/* Mailbox functions for PF */

#include <linux/build_bug.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include "onic.h"
#include "onic_mbox.h"
#include "qdma_device.h"
#include "qdma_register.h"




static void onic_pf_mbox_write_msg(struct qdma_dev *qdev, u32 offset,
				   const struct onic_mbox_msg *msg)
{
	const u32 *words = (const u32 *)msg;
	int i;

	BUILD_BUG_ON(sizeof(*msg) != ONIC_MBOX_MSG_SIZE);

	for (i = 0; i < ONIC_MBOX_MSG_SIZE / sizeof(u32); i++)
		qdma_write_reg(qdev, offset + i * sizeof(u32), words[i]);
}

static void onic_pf_mbox_read_msg(struct qdma_dev *qdev, u32 offset,
				  struct onic_mbox_msg *msg)
{
	u32 *words = (u32 *)msg;
	int i;

	BUILD_BUG_ON(sizeof(*msg) != ONIC_MBOX_MSG_SIZE);

	memset(msg, 0, sizeof(*msg));

	for (i = 0; i < ONIC_MBOX_MSG_SIZE / sizeof(u32); i++)
		words[i] = qdma_read_reg(qdev, offset + i * sizeof(u32));
}
static struct onic_vf_resource *
onic_pf_mbox_find_vf_resource(struct onic_private *priv, u16 func_id)
{
	int i;

	for (i = 0; i < priv->num_vfs; i++) {
		struct onic_vf_resource *res = &priv->vf_res[i];

		if (res->enabled && res->func_id == func_id)
			return res;
	}

	return NULL;
}

static int onic_pf_mbox_init_tx_queue(struct onic_private *priv,
				      u16 src_func_id,
				      const struct onic_mbox_msg *req,
				      struct onic_mbox_msg *resp)
{
	
	struct qdma_dev *pf_qdev = (struct qdma_dev *)priv->hw.qdma;
	struct onic_vf_resource *res;
	struct qdma_dev vf_qdev = {0};
	struct onic_qdma_h2c_param param = {0};
	u32 local_qid = req->data.txq_init.local_qid;
	int err;

	memset(resp, 0, sizeof(*resp));
	resp->hdr.opcode = ONIC_MBOX_OP_TX_QUEUE_INIT_RESP;
	resp->hdr.status = ONIC_MBOX_STS_ERR;
	resp->hdr.seq = req->hdr.seq;
	resp->hdr.len = sizeof(resp->data.txq_resp);

	res = onic_pf_mbox_find_vf_resource(priv, src_func_id);
	if (!res)
		return -ENOENT;

	if (req->hdr.len != sizeof(req->data.txq_init))
		return -EINVAL;

	if (local_qid >= res->qmax)
		return -ERANGE;

	if (!req->data.txq_init.desc_dma_addr)
		return -EINVAL;

	if (!pf_qdev || !pf_qdev->addr)
		return -ENODEV;
		
	vf_qdev.pdev = priv->pdev;
	vf_qdev.addr = pf_qdev->addr;
	vf_qdev.func_id = res->func_id;
	vf_qdev.q_base = res->qbase;
	vf_qdev.num_queues = res->qmax;

	param.rngcnt_idx = req->data.txq_init.rngcnt_idx;
	param.dma_addr = (dma_addr_t)req->data.txq_init.desc_dma_addr;
	param.vid = req->data.txq_init.vector;

	err = onic_qdma_init_tx_queue((unsigned long)&vf_qdev,
				      local_qid, &param);
	if (err)
		return err;

	resp->hdr.status = ONIC_MBOX_STS_OK;
	resp->data.txq_resp.func_id = res->func_id;
	resp->data.txq_resp.local_qid = local_qid;
	resp->data.txq_resp.global_qid = res->qbase + local_qid;

	dev_info(&priv->pdev->dev,
		 "PF init VF TX queue: func_id=%u local_qid=%u global_qid=%u dma=%pad\n",
		 res->func_id, local_qid, res->qbase + local_qid,
		 &param.dma_addr);

	return 0;
}


static int
onic_pf_mbox_make_queue_res_resp(struct onic_private *priv, u16 src_func_id,
				 u32 seq, struct onic_mbox_msg *resp)
{
	struct onic_vf_resource *res;

	memset(resp, 0, sizeof(*resp));

	resp->hdr.opcode = ONIC_MBOX_OP_QUEUE_RES_RESP;
	resp->hdr.status = ONIC_MBOX_STS_ERR;
	resp->hdr.seq = seq;

	res = onic_pf_mbox_find_vf_resource(priv, src_func_id);
	if (!res)
		return -ENOENT;

	resp->hdr.status = ONIC_MBOX_STS_OK;
	resp->hdr.len = sizeof(resp->data.qres);

	resp->data.qres.func_id = res->func_id;
	resp->data.qres.qbase = res->qbase;
	resp->data.qres.qmax = res->qmax;

	return 0;
}

int onic_pf_mbox_process_one(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	struct onic_mbox_msg req;
	struct onic_mbox_msg resp;
	u32 status;
	u16 src_func_id;
	int err;

	if (!qdev || !qdev->addr)
		return -ENODEV;

	status = qdma_read_reg(qdev, QDMA_PF_MBOX_STS);
	if (!(status & QDMA_MBOX_STS_I_MSG_MASK))
		return 0;

	src_func_id = BITFIELD_GET(QDMA_MBOX_STS_CUR_SRC_FN_MASK, status);

	/*
	 * PF inbox/outbox registers are indirect. Select the source VF
	 * before reading its request or writing its response.
	 */
	qdma_write_reg(qdev, QDMA_PF_MBOX_TARGET_FN, src_func_id);

	status = qdma_read_reg(qdev, QDMA_PF_MBOX_STS);
	if (status & QDMA_MBOX_STS_O_MSG_MASK) {
		dev_warn(&priv->pdev->dev,
			 "PF mbox response still pending for func_id=%u\n",
			 src_func_id);
		return -EBUSY;
	}

	onic_pf_mbox_read_msg(qdev, QDMA_PF_MBOX_IN_MSG, &req);

	dev_info(&priv->pdev->dev,
		"PF mbox request: src_func=%u opcode=%u status=%u seq=%u len=%u\n",
		src_func_id, req.hdr.opcode, req.hdr.status,
		req.hdr.seq, req.hdr.len);

	memset(&resp, 0, sizeof(resp));
	resp.hdr.opcode = ONIC_MBOX_OP_QUEUE_RES_RESP;
	resp.hdr.status = ONIC_MBOX_STS_ERR;
	resp.hdr.seq = req.hdr.seq;

	switch (req.hdr.opcode) {
		case ONIC_MBOX_OP_GET_QUEUE_RES:
			if (req.hdr.len != 0) {
				err = -EINVAL;
				break;
			}

			err = onic_pf_mbox_make_queue_res_resp(priv, src_func_id,
								req.hdr.seq, &resp);
			break;
		case ONIC_MBOX_OP_TX_QUEUE_INIT:
			err = onic_pf_mbox_init_tx_queue(priv, src_func_id, &req, &resp);
			break;
		default:
			err = -EOPNOTSUPP;
			break;
	}

	/* Release the VF request after its contents have been copied. */
	qdma_write_reg(qdev, QDMA_PF_MBOX_CMD, QDMA_MBOX_CMD_RCV);



	onic_pf_mbox_write_msg(qdev, QDMA_PF_MBOX_OUT_MSG, &resp);
	qdma_write_reg(qdev, QDMA_PF_MBOX_CMD, QDMA_MBOX_CMD_SEND);
	// dev_info(&priv->pdev->dev,
	//  "PF mbox response sent: src_func=%u opcode=%u status=%u seq=%u len=%u sts=0x%08x\n",
	//  src_func_id, resp.hdr.opcode, resp.hdr.status,
	//  resp.hdr.seq, resp.hdr.len,
	//  qdma_read_reg(qdev, QDMA_PF_MBOX_STS));
	if (err) {
		dev_warn(&priv->pdev->dev,
			 "PF mbox rejected request: func_id=%u opcode=%u err=%d\n",
			 src_func_id, req.hdr.opcode, err);
	} else if (req.hdr.opcode == ONIC_MBOX_OP_GET_QUEUE_RES) {
		dev_info(&priv->pdev->dev,
			"PF mbox queue resource: func_id=%u qbase=%u qmax=%u\n",
			resp.data.qres.func_id,
			resp.data.qres.qbase,
			resp.data.qres.qmax);
	} else if (req.hdr.opcode == ONIC_MBOX_OP_TX_QUEUE_INIT) {
		dev_info(&priv->pdev->dev,
			"PF mbox TX queue init done: func_id=%u local_qid=%u global_qid=%u\n",
			resp.data.txq_resp.func_id,
			resp.data.txq_resp.local_qid,
			resp.data.txq_resp.global_qid);
	}

	return 1;
}
static int onic_pf_mbox_process_acks(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	u32 status;
	int processed = 0;
	int i;

	if (!qdev || !qdev->addr)
		return -ENODEV;

	status = qdma_read_reg(qdev, QDMA_PF_MBOX_STS);
	if (!(status & QDMA_MBOX_STS_ACK_MASK))
		return 0;

	for (i = 0; i < QDMA_PF_MBOX_ACK_REGS; i++) {
		u32 ack = qdma_read_reg(qdev, QDMA_PF_MBOX_ACK(i));

		if (!ack)
			continue;

		/* ACK registers are RW1C: write observed bits back to clear. */
		qdma_write_reg(qdev, QDMA_PF_MBOX_ACK(i), ack);
		processed += hweight32(ack);
	}

	return processed;
}

static int onic_pf_mbox_drop_stale_requests(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	u32 status;
	u16 src_func_id;
	int dropped;

	if (!qdev || !qdev->addr)
		return -ENODEV;

	for (dropped = 0; dropped < 256; dropped++) {
		status = qdma_read_reg(qdev, QDMA_PF_MBOX_STS);
		if (!(status & QDMA_MBOX_STS_I_MSG_MASK))
			return dropped;

		src_func_id =
			BITFIELD_GET(QDMA_MBOX_STS_CUR_SRC_FN_MASK, status);

		qdma_write_reg(qdev, QDMA_PF_MBOX_TARGET_FN, src_func_id);
		qdma_write_reg(qdev, QDMA_PF_MBOX_CMD, QDMA_MBOX_CMD_RCV);

		dev_warn(&priv->pdev->dev,
			 "Dropped stale PF mailbox request from func_id=%u\n",
			 src_func_id);
	}

	return -EIO;
}

int onic_pf_mbox_process_pending(struct onic_private *priv)
{
	int processed = 0;
	int err;

	for (;;) {
		err = onic_pf_mbox_process_acks(priv);
		if (err < 0)
			return err;

		processed += err;

		err = onic_pf_mbox_process_one(priv);
		if (err < 0)
			return err;

		if (!err)
			break;

		processed++;
	}

	return processed;
}

// void onic_pf_mbox_irq_disable(struct onic_private *priv)
// {
// 	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

// 	if (!qdev || !qdev->addr)
// 		return;

// 	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL, 0);
// }

void onic_pf_mbox_irq_disable(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

	if (!qdev || !qdev->addr)
		return;

	// dev_info(&priv->pdev->dev,
	// 	 "PF mbox IRQ disable: caller=%pS sts=0x%08x ctrl_before=0x%08x\n",
	// 	 __builtin_return_address(0),
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_STS),
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_CTRL));

	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL, 0);

	// dev_info(&priv->pdev->dev,
	// 	 "PF mbox IRQ disable done: ctrl_after=0x%08x\n",
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_CTRL));
}

// void onic_pf_mbox_irq_enable(struct onic_private *priv)
// {
// 	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

// 	if (!qdev || !qdev->addr)
// 		return;
// 	// Enable mailbox interrupt
// 	// Ghi 1 vào bit0
// 	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL,
// 		       QDMA_MBOX_INTR_CTRL_EN);
// }

void onic_pf_mbox_irq_enable(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

	if (!qdev || !qdev->addr)
		return;

	// dev_info(&priv->pdev->dev,
	// 	 "PF mbox IRQ enable: caller=%pS sts=0x%08x ctrl_before=0x%08x\n",
	// 	 __builtin_return_address(0),
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_STS),
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_CTRL));

	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL,
		       QDMA_MBOX_INTR_CTRL_EN);

	// dev_info(&priv->pdev->dev,
	// 	 "PF mbox IRQ enable done: ctrl_after=0x%08x\n",
	// 	 qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_CTRL));
}

int onic_pf_mbox_irq_init(struct onic_private *priv, u16 vector)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	int dropped, err;
	if (!qdev || !qdev->addr)
		return -ENODEV;

	onic_pf_mbox_irq_disable(priv);

	dropped = onic_pf_mbox_drop_stale_requests(priv);
	if (dropped < 0)
		return dropped;
	err = onic_pf_mbox_process_pending(priv);
	if (err < 0)
		return err;

	if (err)
		dev_info(&priv->pdev->dev,
				"PF mbox drained %d stale event(s) before IRQ enable\n", err);
	// Ghi interrupt vector number dùng cho mailbox
	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_VEC, vector);
	// Enable mailbox interrupt
	onic_pf_mbox_irq_enable(priv);

	// dev_info(&priv->pdev->dev,
	// 	"PF mbox IRQ enabled: vector_index=%u linux_irq=%d sts=0x%08x vec=0x%08x ctrl=0x%08x\n",
	// 	vector, pci_irq_vector(priv->pdev, vector),
	// 	qdma_read_reg(qdev, QDMA_PF_MBOX_STS),
	// 	qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_VEC),
	// 	qdma_read_reg(qdev, QDMA_PF_MBOX_INTR_CTRL));


	return 0;
}

