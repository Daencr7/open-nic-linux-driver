/* Mailbox functions for PF */

#include <linux/build_bug.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include "onic.h"
#include "onic_mbox.h"
#include "qdma_device.h"
#include "qdma_register.h"
#include "onic_hardware.h"

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
	memcpy(resp->data.qres.mac, res->mac, ETH_ALEN);

	return 0;
}

static void
onic_pf_mbox_init_queue_cmd_resp(struct onic_mbox_msg *resp, u32 opcode,
                                 u32 seq, u32 qid, u32 dir)
{
	memset(resp, 0, sizeof(*resp));
	resp->hdr.opcode = opcode;
	resp->hdr.status = ONIC_MBOX_STS_ERR;
	resp->hdr.seq = seq;
	resp->hdr.len = sizeof(resp->data.qcmd_resp);
	resp->data.qcmd_resp.qid = qid;
	resp->data.qcmd_resp.dir = dir;
}

static dma_addr_t onic_pf_mbox_dma_addr(u32 lo, u32 hi)
{
	return (dma_addr_t)(((u64)hi << 32) | lo);
}

static int
onic_pf_mbox_get_vf_qdev(struct onic_private *priv, u16 src_func_id,
			 u32 qid, struct qdma_dev *vf_qdev)
{
	struct qdma_dev *pf_qdev = (struct qdma_dev *)priv->hw.qdma;
	struct onic_vf_resource *res;

	if (!pf_qdev || !pf_qdev->addr)
		return -ENODEV;

	res = onic_pf_mbox_find_vf_resource(priv, src_func_id);
	if (!res)
		return -ENOENT;

	if (qid >= res->qmax)
		return -ERANGE;

	memset(vf_qdev, 0, sizeof(*vf_qdev));
	vf_qdev->pdev = priv->pdev;
	vf_qdev->addr = pf_qdev->addr;
	vf_qdev->func_id = res->func_id;
	vf_qdev->q_base = res->qbase;
	vf_qdev->num_queues = res->qmax;

	return 0;
}

static int
onic_pf_mbox_config_queue(struct onic_private *priv, u16 src_func_id,
			  const struct onic_mbox_msg *req,
			  struct onic_mbox_msg *resp)
{
	const struct onic_mbox_queue_cfg *cfg = &req->data.qcfg;
	struct qdma_dev vf_qdev;
	dma_addr_t desc_dma;
	int err;

	onic_pf_mbox_init_queue_cmd_resp(resp, ONIC_MBOX_OP_CONFIG_QUEUE_RESP,
					req->hdr.seq, cfg->qid, cfg->dir);

	if (req->hdr.len != sizeof(*cfg) || cfg->reserved)
		return -EINVAL;

	err = onic_pf_mbox_get_vf_qdev(priv, src_func_id, cfg->qid, &vf_qdev);
	if (err)
		return err;

	if (cfg->vector >= vf_qdev.num_queues ||
	    cfg->rngcnt_idx >= QDMA_NUM_DESC_RNGCNT)
		return -ERANGE;

	desc_dma = onic_pf_mbox_dma_addr(cfg->desc_dma_lo, cfg->desc_dma_hi);
	if (!desc_dma || !IS_ALIGNED(desc_dma, 64))
		return -EINVAL;

	switch (cfg->dir) {
	case ONIC_MBOX_QUEUE_DIR_TX: {
		struct onic_qdma_h2c_param param = {
			.rngcnt_idx = cfg->rngcnt_idx,
			.dma_addr = desc_dma,
			.vid = cfg->vector,
		};

		err = onic_qdma_init_tx_queue((unsigned long)&vf_qdev,
					     cfg->qid, &param);
		break;
	}

	case ONIC_MBOX_QUEUE_DIR_RX: {
		dma_addr_t cmpl_dma;
		struct onic_qdma_c2h_param param;

		if (cfg->bufsz_idx >= QDMA_NUM_C2H_BUFSZ ||
		    cfg->cmpl_rngcnt_idx >= QDMA_NUM_DESC_RNGCNT ||
		    cfg->cmpl_desc_sz > 3)
			return -ERANGE;

		cmpl_dma = onic_pf_mbox_dma_addr(cfg->cmpl_dma_lo,
						cfg->cmpl_dma_hi);
		if (!cmpl_dma || !IS_ALIGNED(cmpl_dma, 64))
			return -EINVAL;

		memset(&param, 0, sizeof(param));
		param.bufsz_idx = cfg->bufsz_idx;
		param.desc_rngcnt_idx = cfg->rngcnt_idx;
		param.cmpl_rngcnt_idx = cfg->cmpl_rngcnt_idx;
		param.cmpl_desc_sz = cfg->cmpl_desc_sz;
		param.desc_dma_addr = desc_dma;
		param.cmpl_dma_addr = cmpl_dma;
		param.vid = cfg->vector;

		err = onic_qdma_init_rx_queue((unsigned long)&vf_qdev,
					     cfg->qid, &param);
		break;
	}

	default:
		return -EINVAL;
	}

	if (!err)
		resp->hdr.status = ONIC_MBOX_STS_OK;

	return err;
}

static int
onic_pf_mbox_clear_queue(struct onic_private *priv, u16 src_func_id,
			 const struct onic_mbox_msg *req,
			 struct onic_mbox_msg *resp)
{
	const struct onic_mbox_queue_clear *clear = &req->data.qclear;
	struct qdma_dev vf_qdev;
	int err;

	onic_pf_mbox_init_queue_cmd_resp(resp, ONIC_MBOX_OP_CLEAR_QUEUE_RESP,
					req->hdr.seq, clear->qid, clear->dir);

	if (req->hdr.len != sizeof(*clear))
		return -EINVAL;

	err = onic_pf_mbox_get_vf_qdev(priv, src_func_id, clear->qid,
				      &vf_qdev);
	if (err)
		return err;

	switch (clear->dir) {
	case ONIC_MBOX_QUEUE_DIR_TX:
		onic_qdma_clear_tx_queue((unsigned long)&vf_qdev, clear->qid);
		break;

	case ONIC_MBOX_QUEUE_DIR_RX:
		onic_qdma_clear_rx_queue((unsigned long)&vf_qdev, clear->qid);
		break;

	default:
		return -EINVAL;
	}

	resp->hdr.status = ONIC_MBOX_STS_OK;
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
		case ONIC_MBOX_OP_CONFIG_QUEUE:
			err = onic_pf_mbox_config_queue(priv, src_func_id, &req, &resp);
			break;

		case ONIC_MBOX_OP_CLEAR_QUEUE:
			err = onic_pf_mbox_clear_queue(priv, src_func_id, &req, &resp);
			break;
		default:
			err = -EOPNOTSUPP;
			break;
		}

	/* Release the VF request after its contents have been copied. */
	qdma_write_reg(qdev, QDMA_PF_MBOX_CMD, QDMA_MBOX_CMD_RCV);

	onic_pf_mbox_write_msg(qdev, QDMA_PF_MBOX_OUT_MSG, &resp);
	qdma_write_reg(qdev, QDMA_PF_MBOX_CMD, QDMA_MBOX_CMD_SEND);

	if (err)
		dev_warn(&priv->pdev->dev,
			 "PF mbox rejected request: func_id=%u opcode=%u err=%d\n",
			 src_func_id, req.hdr.opcode, err);
	else if (resp.hdr.opcode == ONIC_MBOX_OP_QUEUE_RES_RESP)
		dev_info(&priv->pdev->dev,
			 "PF mbox queue resource: func_id=%u qbase=%u qmax=%u\n",
			 resp.data.qres.func_id,
			 resp.data.qres.qbase,
			 resp.data.qres.qmax);
	else
		dev_info(&priv->pdev->dev,
			 "PF mbox queue command: func_id=%u opcode=%u qid=%u dir=%u\n",
			 src_func_id, resp.hdr.opcode,
			 resp.data.qcmd_resp.qid,
			 resp.data.qcmd_resp.dir);

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

void onic_pf_mbox_irq_disable(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

	if (!qdev || !qdev->addr)
		return;

	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL, 0);
}

void onic_pf_mbox_irq_enable(struct onic_private *priv)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;

	if (!qdev || !qdev->addr)
		return;

	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_CTRL,
		       QDMA_MBOX_INTR_CTRL_EN);
}

int onic_pf_mbox_irq_init(struct onic_private *priv, u16 vector)
{
	struct qdma_dev *qdev = (struct qdma_dev *)priv->hw.qdma;
	int dropped;
	if (!qdev || !qdev->addr)
		return -ENODEV;

	onic_pf_mbox_irq_disable(priv);

	dropped = onic_pf_mbox_drop_stale_requests(priv);
	if (dropped < 0)
		return dropped;

	qdma_write_reg(qdev, QDMA_PF_MBOX_INTR_VEC, vector);
	onic_pf_mbox_irq_enable(priv);



	return 0;
}
