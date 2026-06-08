/* Mailbox functions for VF */

#include <linux/build_bug.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/random.h>
#include "onic.h"
#include "onic_vf_mbox.h"
#include "qdma_register.h"

static void onic_vf_mbox_read_msg(struct onic_private *priv, u32 offset,
				  struct onic_mbox_msg *msg)
{
	u32 *words = (u32 *)msg;
	int i;

	BUILD_BUG_ON(sizeof(*msg) != ONIC_MBOX_MSG_SIZE);

	memset(msg, 0, sizeof(*msg));

	for (i = 0; i < ONIC_MBOX_MSG_SIZE / sizeof(u32); i++)
		words[i] = onic_vf_read_bar0(priv,
					     offset + i * sizeof(u32));
}

static void onic_vf_mbox_write_msg(struct onic_private *priv, u32 offset,
				   const struct onic_mbox_msg *msg)
{
	const u32 *words = (const u32 *)msg;
	int i;

	BUILD_BUG_ON(sizeof(*msg) != ONIC_MBOX_MSG_SIZE);

	for (i = 0; i < ONIC_MBOX_MSG_SIZE / sizeof(u32); i++)
		onic_vf_write_bar0(priv, offset + i * sizeof(u32), words[i]);
}

int onic_vf_mbox_process_one(struct onic_private *priv)
{
	struct onic_mbox_msg *resp = &priv->vf_hw.mbox_resp;
	u32 status;

	status = onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS);
	if (!(status & QDMA_MBOX_STS_I_MSG_MASK))
		return 0;

	onic_vf_mbox_read_msg(priv, QDMA_VF_MBOX_IN_MSG, resp);
	dev_info(&priv->pdev->dev,
		"VF mbox response: opcode=%u status=%u seq=%u len=%u\n",
		resp->hdr.opcode, resp->hdr.status,
		resp->hdr.seq, resp->hdr.len);
	/* Tell PF that VF consumed its response. */
	onic_vf_write_bar0(priv, QDMA_VF_MBOX_CMD, QDMA_MBOX_CMD_RCV);
	dev_info(&priv->pdev->dev,
		"VF mbox response acked: sts=0x%08x\n",
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS));
	complete(&priv->vf_hw.mbox_done);

	return 1;
}

static irqreturn_t onic_vf_mbox_irq_handler(int irq, void *data)
{
	struct onic_private *priv = data;
	dev_info(&priv->pdev->dev,
		"VF mbox IRQ top: sts=0x%08x ctrl=0x%08x\n",
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS),
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_CTRL));
	onic_vf_write_bar0(priv, QDMA_VF_MBOX_INTR_CTRL, 0);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t onic_vf_mbox_irq_thread(int irq, void *data)
{
	struct onic_private *priv = data;
	int err;

	dev_info(&priv->pdev->dev,
		"VF mbox IRQ thread start: sts=0x%08x\n",
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS));

	err = onic_vf_mbox_process_one(priv);

	dev_info(&priv->pdev->dev,
		"VF mbox IRQ thread processed: err=%d sts=0x%08x\n",
		err, onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS));

	onic_vf_write_bar0(priv, QDMA_VF_MBOX_INTR_CTRL,
			   QDMA_MBOX_INTR_CTRL_EN);

	if (err < 0)
		dev_err(&priv->pdev->dev,
			"VF mailbox processing failed: %d\n", err);

	return IRQ_HANDLED;
}

int onic_vf_mbox_irq_init(struct onic_private *priv, u16 vector)
{
	struct pci_dev *pdev = priv->pdev;
	struct onic_vf_hardware *vf_hw = &priv->vf_hw;
	int err;

	init_completion(&vf_hw->mbox_done);
	mutex_init(&vf_hw->mbox_lock);
	vf_hw->mbox_seq = get_random_u32();
	err = request_threaded_irq(pci_irq_vector(pdev, vector),
				   onic_vf_mbox_irq_handler,
				   onic_vf_mbox_irq_thread,
				   0, "onic-vf-mbox", priv);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request VF mailbox IRQ: %d\n", err);
		return err;
	}
	dev_info(&pdev->dev,
	 "VF mbox IRQ requested: vector_index=%u linux_irq=%d\n",
	 vector, pci_irq_vector(pdev, vector));

	vf_hw->mbox_vector = vector;
	vf_hw->mbox_irq_allocated = true;

	onic_vf_write_bar0(priv, QDMA_VF_MBOX_INTR_VEC, vector);
	onic_vf_write_bar0(priv, QDMA_VF_MBOX_INTR_CTRL,
			   QDMA_MBOX_INTR_CTRL_EN);

	dev_info(&pdev->dev,
		"VF mbox IRQ enabled: sts=0x%08x vec=0x%08x ctrl=0x%08x\n",
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS),
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_VEC),
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_CTRL));
	return 0;
}

void onic_vf_mbox_irq_clear(struct onic_private *priv)
{
	struct pci_dev *pdev = priv->pdev;
	struct onic_vf_hardware *vf_hw = &priv->vf_hw;

	if (!vf_hw->mbox_irq_allocated)
		return;

	onic_vf_write_bar0(priv, QDMA_VF_MBOX_INTR_CTRL, 0);

	free_irq(pci_irq_vector(pdev, vf_hw->mbox_vector), priv);

	vf_hw->mbox_irq_allocated = false;
}

static int onic_vf_mbox_drop_stale_responses(struct onic_private *priv)
{
	struct onic_mbox_msg stale;
	u32 status;
	int dropped;

	for (dropped = 0; dropped < 16; dropped++) {
		status = onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS);
		if (!(status & QDMA_MBOX_STS_I_MSG_MASK))
			return dropped;

		onic_vf_mbox_read_msg(priv, QDMA_VF_MBOX_IN_MSG, &stale);
		onic_vf_write_bar0(priv, QDMA_VF_MBOX_CMD,
				   QDMA_MBOX_CMD_RCV);

		dev_warn(&priv->pdev->dev,
			 "Dropped stale VF mailbox response: opcode=%u seq=%u\n",
			 stale.hdr.opcode, stale.hdr.seq);
	}

	return -EIO;
}

int onic_vf_mbox_get_queue_resource(struct onic_private *priv)
{
	struct onic_vf_hardware *vf_hw = &priv->vf_hw;
	struct onic_mbox_msg req = {0};
	struct onic_mbox_msg *resp = &vf_hw->mbox_resp;
	unsigned long timeout;
	int err = 0;
	u32 status;

	mutex_lock(&vf_hw->mbox_lock);
	err = onic_vf_mbox_drop_stale_responses(priv);
	if (err < 0)
		goto out_unlock;
		
	status = onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS);
	if (status & QDMA_MBOX_STS_O_MSG_MASK) {
		err = -EBUSY;
		goto out_unlock;
	}

	reinit_completion(&vf_hw->mbox_done);
	memset(resp, 0, sizeof(*resp));

	req.hdr.opcode = ONIC_MBOX_OP_GET_QUEUE_RES;
	req.hdr.seq = ++vf_hw->mbox_seq;
	req.hdr.len = 0;
	dev_info(&priv->pdev->dev,
		"VF mbox send prepare: opcode=%u seq=%u sts=0x%08x vec=0x%08x ctrl=0x%08x\n",
		req.hdr.opcode, req.hdr.seq,
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS),
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_VEC),
		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_CTRL));

	onic_vf_mbox_write_msg(priv, QDMA_VF_MBOX_OUT_MSG, &req);
	onic_vf_write_bar0(priv, QDMA_VF_MBOX_CMD, QDMA_MBOX_CMD_SEND);

	dev_info(&priv->pdev->dev,
	 "VF mbox SEND posted: sts=0x%08x\n",
	 onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS));

	timeout = wait_for_completion_timeout(&vf_hw->mbox_done,
					      msecs_to_jiffies(1000));
	// if (!timeout) {
	// 	err = -ETIMEDOUT;
	// 	goto out_unlock;
	// }

	if (!timeout) {
		status = onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS);

		dev_err(&priv->pdev->dev,
			"VF mailbox timeout: sts=0x%08x vec=0x%08x ctrl=0x%08x\n",
			status,
			onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_VEC),
			onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_CTRL));

	// 	/*
	// 	* Bring-up fallback: inspect a response that reached VF inbox
	// 	* even if VF mailbox MSI-X routing did not fire.
	// 	*/
	// 	if (status & QDMA_MBOX_STS_I_MSG_MASK) {
	// 		err = onic_vf_mbox_process_one(priv);
	// 		if (err > 0) {
	// 			err = 0;
	// 			goto validate_response;
	// 		}
	// 	}

		err = -ETIMEDOUT;
		goto out_unlock;
	}
// validate_response:
	if (resp->hdr.opcode != ONIC_MBOX_OP_QUEUE_RES_RESP ||
	    resp->hdr.seq != req.hdr.seq ||
	    resp->hdr.status != ONIC_MBOX_STS_OK ||
	    resp->hdr.len != sizeof(resp->data.qres)) {
		err = -EPROTO;
		goto out_unlock;
	}
	if (!resp->data.qres.qmax ||
		resp->data.qres.qmax > ONIC_MAX_QUEUES) {
		err = -ERANGE;
		goto out_unlock;
	}
	vf_hw->func_id = resp->data.qres.func_id;
	vf_hw->qbase = resp->data.qres.qbase;
	vf_hw->qmax = resp->data.qres.qmax;
	vf_hw->resource_valid = true;

	dev_info(&priv->pdev->dev,
		 "VF queue resource: func_id=%u qbase=%u qmax=%u\n",
		 vf_hw->func_id, vf_hw->qbase, vf_hw->qmax);

out_unlock:
	mutex_unlock(&vf_hw->mbox_lock);

	return err;
}