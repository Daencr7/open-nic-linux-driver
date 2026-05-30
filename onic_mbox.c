/* Mailbox functions for PF */
/**
 * - nhận mailbox message từ VF
 * - xử lý GET_CONFIG
 * - xử lý SET_MAC
 * - xử lý SET_MTU
 * - xử lý GET_LINK
 * - trả reply cho VF
 */
#include "onic_mbox.h"


static int onic_pf_mbox_make_get_resource_resp(
	struct onic_private *priv,
	u16 vf_id,
	u16 seq,
	struct onic_mbox_get_resource_resp *resp)
{
	struct onic_vf_resource *res;

	if (vf_id >= priv->num_vfs)
		return -EINVAL;

	res = &priv->vf_res[vf_id];

	if (!res->enabled)
		return -EINVAL;

	memset(resp, 0, sizeof(*resp));

	resp->hdr.magic = ONIC_MBOX_MAGIC;
	resp->hdr.version = ONIC_MBOX_VERSION;
	resp->hdr.opcode = ONIC_MBOX_OP_GET_RESOURCE;
	resp->hdr.vf_id = vf_id;
	resp->hdr.seq = seq;
	resp->hdr.status = ONIC_MBOX_STATUS_OK;
	resp->hdr.len = sizeof(*resp);

	resp->qbase = res->qbase;
	resp->qmax = res->qmax;
	resp->num_tx_queues = res->num_tx_queues;
	resp->num_rx_queues = res->num_rx_queues;
	ether_addr_copy(resp->mac, res->mac);

	return 0;
}

static void onic_mbox_write_msg(void __iomem *base, u32 off,
				const void *msg, size_t len)
{
	size_t i;
	u32 word;

	for (i = 0; i < len; i += 4) {
		word = 0;
		memcpy(&word, (const u8 *)msg + i,
		       min_t(size_t, 4, len - i));
		iowrite32(word, base + off + i);
	}
}

static void onic_mbox_read_msg(void __iomem *base, u32 off,
			       void *msg, size_t len)
{
	size_t i;
	u32 word;

	for (i = 0; i < len; i += 4) {
		word = ioread32(base + off + i);
		memcpy((u8 *)msg + i, &word,
		       min_t(size_t, 4, len - i));
	}
}

int onic_pf_mbox_poll(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	void __iomem *base;
	u32 sts;
	u16 src_fn;
	u16 vf_id;
	struct onic_mbox_hdr hdr;
	struct onic_mbox_get_resource_resp resp;
	int err;

	if (!qdev || !qdev->addr)
		return -ENODEV;

	base = qdev->addr;

	sts = ioread32(base + QDMA_PF_MBOX_STS);

	if (!(sts & QDMA_MBOX_STS_I_MSG_MASK))
		return 0;

	src_fn = FIELD_GET(QDMA_MBOX_STS_CUR_SRC_FN_MASK, sts);

	if (src_fn < 4) {
		dev_err(&priv->pdev->dev,
			"PF mbox invalid src_fn=%u\n", src_fn);
		goto ack_msg;
	}

	vf_id = src_fn - 4;

	memset(&hdr, 0, sizeof(hdr));
	onic_mbox_read_msg(base, QDMA_PF_MBOX_IN_MSG,
			   &hdr, sizeof(hdr));

	dev_info(&priv->pdev->dev,
		 "PF mbox rx: src_fn=%u vf_id=%u magic=0x%x opcode=%u seq=%u len=%u\n",
		 src_fn, vf_id, hdr.magic, hdr.opcode, hdr.seq, hdr.len);

	if (hdr.magic != ONIC_MBOX_MAGIC ||
	    hdr.version != ONIC_MBOX_VERSION) {
		dev_err(&priv->pdev->dev,
			"PF mbox bad header\n");
		goto ack_msg;
	}

	switch (hdr.opcode) {
	case ONIC_MBOX_OP_GET_RESOURCE:
		err = onic_pf_mbox_make_get_resource_resp(priv,
							  vf_id,
							  hdr.seq,
							  &resp);
		if (err) {
			memset(&resp, 0, sizeof(resp));

			resp.hdr.magic = ONIC_MBOX_MAGIC;
			resp.hdr.version = ONIC_MBOX_VERSION;
			resp.hdr.opcode = hdr.opcode;
			resp.hdr.vf_id = vf_id;
			resp.hdr.seq = hdr.seq;
			resp.hdr.status = ONIC_MBOX_STATUS_ERR;
			resp.hdr.len = sizeof(resp);
		}

		iowrite32(src_fn, base + QDMA_PF_MBOX_TARGET_FN);

		onic_mbox_write_msg(base, QDMA_PF_MBOX_OUT_MSG,
				    &resp, sizeof(resp));

		iowrite32(QDMA_MBOX_CMD_SEND,
			  base + QDMA_PF_MBOX_CMD);

		dev_info(&priv->pdev->dev,
			 "PF mbox tx GET_RESOURCE: vf_id=%u qbase=%u qmax=%u txq=%u rxq=%u mac=%pM\n",
			 vf_id,
			 resp.qbase,
			 resp.qmax,
			 resp.num_tx_queues,
			 resp.num_rx_queues,
			 resp.mac);
		break;

	default:
		dev_err(&priv->pdev->dev,
			"PF mbox unknown opcode=%u\n", hdr.opcode);
		break;
	}

ack_msg:
	iowrite32(QDMA_MBOX_CMD_RCV,
		  base + QDMA_PF_MBOX_CMD);

	iowrite32(QDMA_MBOX_CMD_POP,
		  base + QDMA_PF_MBOX_CMD);

	return 1;
}

/* Delayed work function for handling PF mailbox messages */

static void onic_pf_mbox_work(struct work_struct *work)
{
	struct onic_private *priv;

	priv = container_of(to_delayed_work(work),
			    struct onic_private,
			    mbox_work);

	onic_pf_mbox_poll(priv);

	schedule_delayed_work(&priv->mbox_work,
			      msecs_to_jiffies(10));
}

void onic_pf_mbox_start(struct onic_private *priv)
{
	INIT_DELAYED_WORK(&priv->mbox_work,
			  onic_pf_mbox_work);

	schedule_delayed_work(&priv->mbox_work,
			      msecs_to_jiffies(10));
}

void onic_pf_mbox_stop(struct onic_private *priv)
{
	cancel_delayed_work_sync(&priv->mbox_work);
}