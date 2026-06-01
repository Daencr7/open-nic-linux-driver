/**
 * Queue/QDMA just for VF, not shared with PF
 * - init TX queue của VF
 * - init RX queue của VF
 * - clear TX/RX queue
 * - setup descriptor ring
 * - setup completion ring
 * - ring doorbell
 */

/* VF không tự chọn queue.
VF dùng qbase/qmax do PF trả qua mailbox.

Sau khi VF nhận resource từ PF:
qbase = ...
qmax  = ...
VF mới được init queue.

Ở đây phải phân biệt:
local queue index của VF: 0, 1, 2, ...
global QDMA queue ID: qbase + local_index
Ví dụ PF cấp:
VF0 qbase=4 qmax=4
local queue 0 → global queue 4
local queue 1 → global queue 5
local queue 2 → global queue 6
local queue 3 → global queue 7
Trong code VF nên có helper:
static inline u16 onic_vf_global_qid(struct onic_private *priv, u16 local_qid)
{
	return priv->qbase + local_qid;
}
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include "onic.h"
#include "onic_vf_qdma.h"
#include "qdma_device.h"

int onic_vf_qdma_init(struct onic_private *priv)
{
	struct onic_vf_hardware *vf_hw = &priv->vf_hw;
	struct qdma_dev *qdev;

	if (!vf_hw->resource_valid)
		return -EINVAL;

	if (!vf_hw->bar0)
		return -ENODEV;

	if (vf_hw->qdev)
		return -EALREADY;

	qdev = kzalloc(sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	qdev->pdev = priv->pdev;
	qdev->addr = vf_hw->bar0;
	qdev->func_id = vf_hw->func_id;
	qdev->q_base = vf_hw->qbase;
	qdev->num_queues = vf_hw->qmax;

	vf_hw->qdev = qdev;
	vf_hw->num_tx_queues = vf_hw->qmax;
	vf_hw->num_rx_queues = vf_hw->qmax;

	dev_info(&priv->pdev->dev,
		 "VF QDMA state: func_id=%u qbase=%u qmax=%u\n",
		 qdev->func_id, qdev->q_base, qdev->num_queues);

	return 0;
}

void onic_vf_qdma_clear(struct onic_private *priv)
{
	struct onic_vf_hardware *vf_hw = &priv->vf_hw;

	if (!vf_hw->qdev)
		return;

	/* BAR0 belongs to onic_vf_map_bars(), only release the wrapper. */
	vf_hw->qdev->addr = NULL;
	kfree(vf_hw->qdev);
	vf_hw->qdev = NULL;

	vf_hw->num_tx_queues = 0;
	vf_hw->num_rx_queues = 0;
	vf_hw->resource_valid = false;
}

static irqreturn_t onic_vf_q_irq_handler(int irq, void *data)
{
    struct onic_q_vector *vec = data;
    struct onic_private *priv = vec->priv;

    dev_dbg(&priv->pdev->dev,
            "VF queue IRQ: local_qid=%u irq=%d\n",
            vec->vid, irq);

    /*
     * Chưa schedule NAPI ở giai đoạn này vì RX queue chưa được tạo.
     */
    return IRQ_HANDLED;
}

static void onic_vf_clear_one_q_irq(struct onic_private *priv, u16 vid)
{
    struct onic_q_vector *vec = priv->q_vector[vid];

    if (!vec)
        return;

    free_irq(pci_irq_vector(priv->pdev, vid), vec);
    kfree(vec);
    priv->q_vector[vid] = NULL;
}

static int onic_vf_init_one_q_irq(struct onic_private *priv, u16 vid)
{
    struct pci_dev *pdev = priv->pdev;
    struct onic_q_vector *vec;
    int irq;
    int err;

    vec = kzalloc(sizeof(*vec), GFP_KERNEL);
    if (!vec)
        return -ENOMEM;

    vec->priv = priv;
    vec->vid = vid;

    irq = pci_irq_vector(pdev, vid);
    if (irq < 0) {
        kfree(vec);
        return irq;
    }

    err = request_irq(irq, onic_vf_q_irq_handler, 0,
                      "onic-vf-q", vec);
    if (err) {
        kfree(vec);
        return err;
    }

    priv->q_vector[vid] = vec;

    dev_info(&pdev->dev,
             "Setup VF queue IRQ: local_qid=%u vector_index=%u irq=%d\n",
             vid, vid, irq);

    return 0;
}

int onic_vf_q_irq_init(struct onic_private *priv)
{
    int vid;
    int err;

    for (vid = 0; vid < priv->num_q_vectors; vid++) {
        err = onic_vf_init_one_q_irq(priv, vid);
        if (err)
            goto err_clear;
    }

    return 0;

err_clear:
    while (vid--)
        onic_vf_clear_one_q_irq(priv, vid);

    return err;
}

void onic_vf_q_irq_clear(struct onic_private *priv)
{
    int vid;

    for (vid = 0; vid < ONIC_MAX_QUEUES; vid++)
        onic_vf_clear_one_q_irq(priv, vid);
}