/**
 * Queue/QDMA just for VF, not shared with PF
 * - init TX queue của VF
 * - init RX queue của VF
 * - clear TX/RX queue
 * - setup descriptor ring
 * - setup completion ring
 * - ring doorbell
 */

/*
 * VF datapath always uses local qid: 0 .. qmax - 1.
 * Do not add qbase when writing VF doorbell registers.
 * PF uses qbase when configuring global QDMA contexts.
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>



#include "onic.h"
#include "onic_vf_qdma.h"
#include "qdma_device.h"
#include "qdma_register.h"
#include "qdma_export.h"

#define ONIC_VF_TX_RING_COUNT       4096
#define ONIC_VF_RX_RING_COUNT       1024
#define ONIC_VF_CMPL_RING_COUNT     1024

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

static bool onic_vf_qid_valid(struct onic_private *priv, u16 qid)
{
    struct onic_vf_hardware *vf_hw = &priv->vf_hw;

    if (!vf_hw->resource_valid || !vf_hw->qdev || qid >= vf_hw->qmax) {
        dev_warn(&priv->pdev->dev,
                 "Invalid VF local qid=%u qmax=%u\n",
                 qid, vf_hw->qmax);
        return false;
    }

    return true;
}

static void onic_vf_set_desc_pidx(struct onic_private *priv, u16 qid,
                                  u16 pidx, bool c2h)
{
    u32 offset;
    u32 val;

    if (!onic_vf_qid_valid(priv, qid))
        return;

    offset = c2h ? QDMA_OFFSET_VF_DMAP_SEL_C2H_DESC_PIDX
                 : QDMA_OFFSET_VF_DMAP_SEL_H2C_DESC_PIDX;
    offset += qid * 16;

    val = FIELD_SET(QDMA_DMAP_SEL_DESC_PIDX_MASK, pidx);
    onic_vf_write_bar0(priv, offset, val);
}

void onic_vf_set_tx_head(struct onic_private *priv, u16 qid, u16 head)
{
    onic_vf_set_desc_pidx(priv, qid, head, false);
}

void onic_vf_set_rx_head(struct onic_private *priv, u16 qid, u16 head)
{
    onic_vf_set_desc_pidx(priv, qid, head, true);
}

void onic_vf_set_completion_tail(struct onic_private *priv, u16 qid,
                                 u16 tail, u8 irq_arm)
{
    u32 offset;
    u32 val;

    if (!onic_vf_qid_valid(priv, qid))
        return;

    offset = QDMA_OFFSET_VF_DMAP_SEL_CMPL_CIDX + qid * 16;

    val = FIELD_SET(QDMA_DMAP_SEL_CMPL_CIDX_MASK, tail) |
          FIELD_SET(QDMA_DMAP_SEL_CMPL_COUNTER_IDX_MASK, 0) |
          FIELD_SET(QDMA_DMAP_SEL_CMPL_TIMER_IDX_MASK, 0) |
          FIELD_SET(QDMA_DMAP_SEL_CMPL_TRIG_MODE_MASK, 5) |
          FIELD_SET(QDMA_DMAP_SEL_CMPL_STAT_EN_MASK, 1) |
          FIELD_SET(QDMA_DMAP_SEL_CMPL_IRQ_ARM_MASK, irq_arm);

    onic_vf_write_bar0(priv, offset, val);
}

static size_t onic_vf_ring_bytes(u16 count, size_t desc_size,
                                 size_t stat_size)
{
    return ALIGN(desc_size * (count - 1) + stat_size, PAGE_SIZE);
}

static void onic_vf_free_tx_ring(struct onic_private *priv, u16 qid)
{
    struct onic_tx_queue *q = priv->tx_queue[qid];
    size_t size;

    if (!q)
        return;

    size = onic_vf_ring_bytes(q->ring.count, QDMA_H2C_ST_DESC_SIZE,
                              QDMA_WB_STAT_SIZE);
    if (q->ring.desc)
        dma_free_coherent(&priv->pdev->dev, size, q->ring.desc,
                          q->ring.dma_addr);

    kfree(q->buffer);
    kfree(q);
    priv->tx_queue[qid] = NULL;
}

static int onic_vf_alloc_tx_ring(struct onic_private *priv, u16 qid)
{
    struct onic_tx_queue *q;
    size_t size;
    u16 real_count;

    q = kzalloc(sizeof(*q), GFP_KERNEL);
    if (!q)
        return -ENOMEM;

    q->netdev = priv->netdev;
    q->qid = qid;
    q->vector = priv->q_vector[qid % priv->num_q_vectors];

    q->ring.count = ONIC_VF_TX_RING_COUNT;
    real_count = q->ring.count - 1;
    size = onic_vf_ring_bytes(q->ring.count, QDMA_H2C_ST_DESC_SIZE,
                              QDMA_WB_STAT_SIZE);

    q->ring.desc = dma_alloc_coherent(&priv->pdev->dev, size,
                                      &q->ring.dma_addr, GFP_KERNEL);
    if (!q->ring.desc)
        goto err_free_q;

    memset(q->ring.desc, 0, size);
    q->ring.wb = q->ring.desc + QDMA_H2C_ST_DESC_SIZE * real_count;

    q->buffer = kcalloc(real_count, sizeof(*q->buffer), GFP_KERNEL);
    if (!q->buffer)
        goto err_free_dma;

    priv->tx_queue[qid] = q;
    return 0;

err_free_dma:
    dma_free_coherent(&priv->pdev->dev, size, q->ring.desc,
                      q->ring.dma_addr);
err_free_q:
    kfree(q);
    return -ENOMEM;
}

static void onic_vf_free_rx_ring(struct onic_private *priv, u16 qid)
{
    struct onic_rx_queue *q = priv->rx_queue[qid];
    size_t size;

    if (!q)
        return;

    size = onic_vf_ring_bytes(q->desc_ring.count, QDMA_C2H_ST_DESC_SIZE,
                              QDMA_WB_STAT_SIZE);
    if (q->desc_ring.desc)
        dma_free_coherent(&priv->pdev->dev, size, q->desc_ring.desc,
                          q->desc_ring.dma_addr);

    size = onic_vf_ring_bytes(q->cmpl_ring.count, QDMA_C2H_CMPL_SIZE,
                              QDMA_C2H_CMPL_STAT_SIZE);
    if (q->cmpl_ring.desc)
        dma_free_coherent(&priv->pdev->dev, size, q->cmpl_ring.desc,
                          q->cmpl_ring.dma_addr);

    kfree(q->buffer);
    kfree(q);
    priv->rx_queue[qid] = NULL;
}

static int onic_vf_alloc_rx_ring(struct onic_private *priv, u16 qid)
{
    struct onic_rx_queue *q;
    size_t size;
    u16 real_count;

    q = kzalloc(sizeof(*q), GFP_KERNEL);
    if (!q)
        return -ENOMEM;

    q->netdev = priv->netdev;
    q->qid = qid;
    q->vector = priv->q_vector[qid % priv->num_q_vectors];

    q->desc_ring.count = ONIC_VF_RX_RING_COUNT;
    real_count = q->desc_ring.count - 1;
    size = onic_vf_ring_bytes(q->desc_ring.count, QDMA_C2H_ST_DESC_SIZE,
                              QDMA_WB_STAT_SIZE);

    q->desc_ring.desc = dma_alloc_coherent(&priv->pdev->dev, size,
                                           &q->desc_ring.dma_addr,
                                           GFP_KERNEL);
    if (!q->desc_ring.desc)
        goto err_free_q;

    memset(q->desc_ring.desc, 0, size);
    q->desc_ring.wb =
        q->desc_ring.desc + QDMA_C2H_ST_DESC_SIZE * real_count;

    q->buffer = kcalloc(real_count, sizeof(*q->buffer), GFP_KERNEL);
    if (!q->buffer)
        goto err_free_desc;

    q->cmpl_ring.count = ONIC_VF_CMPL_RING_COUNT;
    size = onic_vf_ring_bytes(q->cmpl_ring.count, QDMA_C2H_CMPL_SIZE,
                              QDMA_C2H_CMPL_STAT_SIZE);

    q->cmpl_ring.desc = dma_alloc_coherent(&priv->pdev->dev, size,
                                           &q->cmpl_ring.dma_addr,
                                           GFP_KERNEL);
    if (!q->cmpl_ring.desc)
        goto err_free_buffer;

    memset(q->cmpl_ring.desc, 0, size);
    q->cmpl_ring.wb =
        q->cmpl_ring.desc +
        QDMA_C2H_CMPL_SIZE * (q->cmpl_ring.count - 1);
    q->cmpl_ring.color = 1;

    priv->rx_queue[qid] = q;
    return 0;

err_free_buffer:
    kfree(q->buffer);
err_free_desc:
    size = onic_vf_ring_bytes(q->desc_ring.count,
                              QDMA_C2H_ST_DESC_SIZE,
                              QDMA_WB_STAT_SIZE);
    dma_free_coherent(&priv->pdev->dev, size, q->desc_ring.desc,
                      q->desc_ring.dma_addr);
err_free_q:
    kfree(q);
    return -ENOMEM;
}

int onic_vf_rings_init(struct onic_private *priv)
{
    int qid;
    int err;

    if (!priv->num_q_vectors)
        return -EINVAL;

    for (qid = 0; qid < priv->num_tx_queues; qid++) {
        err = onic_vf_alloc_tx_ring(priv, qid);
        if (err)
            goto err_clear;
    }

    for (qid = 0; qid < priv->num_rx_queues; qid++) {
        err = onic_vf_alloc_rx_ring(priv, qid);
        if (err)
            goto err_clear;
    }

    dev_info(&priv->pdev->dev,
             "VF inactive rings allocated: tx=%u rx=%u\n",
             priv->num_tx_queues, priv->num_rx_queues);
    return 0;

err_clear:
    onic_vf_rings_clear(priv);
    return err;
}

void onic_vf_rings_clear(struct onic_private *priv)
{
    int qid;
    bool allocated = false;

    for (qid = 0; qid < ONIC_MAX_QUEUES; qid++) {
        allocated |= !!priv->tx_queue[qid] || !!priv->rx_queue[qid];
        onic_vf_free_tx_ring(priv, qid);
        onic_vf_free_rx_ring(priv, qid);
    }

    if (allocated)
        dev_info(&priv->pdev->dev, "VF inactive rings released\n");
}