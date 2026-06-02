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
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/percpu.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <net/page_pool/helpers.h>
#include <net/page_pool/types.h>
#else
#include <net/page_pool.h>
#endif
#include <linux/kernel.h>
#include <linux/bitops.h>

#include "onic.h"
#include "onic_vf_qdma.h"
#include "onic_mbox.h"
#include "onic_vf_mbox.h"
#include "qdma_device.h"
#include "qdma_register.h"
#include "qdma_export.h"

#define ONIC_VF_TX_RING_COUNT       4096
#define ONIC_VF_RX_RING_COUNT       1024
#define ONIC_VF_CMPL_RING_COUNT     1024
#define ONIC_VF_TX_RNGCNT_IDX       0
#define ONIC_VF_TX_CTXT_CONFIGURED  31
#define ONIC_VF_RX_DESC_RNGCNT_IDX  8
#define ONIC_VF_RX_CMPL_RNGCNT_IDX  8
#define ONIC_VF_RX_BUFSZ_IDX        4
#define ONIC_VF_RX_CMPL_DESC_SZ     0
#define ONIC_VF_RX_CTXT_CONFIGURED  31
#define ONIC_VF_RX_DESC_STEP        256
#define ONIC_VF_RX_NAPI_ENABLED     30

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
    struct onic_rx_queue *q = READ_ONCE(priv->rx_queue[vec->vid]);

    if (!q || !test_bit(ONIC_VF_RX_NAPI_ENABLED, q->state))
        return IRQ_HANDLED;

    napi_schedule_irqoff(&q->napi);
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

static u16 onic_vf_ring_real_count(const struct onic_ring *ring)
{
    return ring->count - 1;
}

static void onic_vf_ring_increment_clean(struct onic_ring *ring)
{
    ring->next_to_clean++;
    if (ring->next_to_clean == onic_vf_ring_real_count(ring))
        ring->next_to_clean = 0;
}

void onic_vf_tx_clean(struct onic_private *priv, struct onic_tx_queue *q)
{
    struct onic_ring *ring;
    struct qdma_wb_stat wb;
    u16 real_count;
    u16 work = 0;

    if (!q || !q->ring.wb || !q->buffer)
        return;

    if (test_and_set_bit(0, q->state))
        return;

    ring = &q->ring;
    real_count = onic_vf_ring_real_count(ring);

    dma_rmb();
    qdma_unpack_wb_stat(&wb, ring->wb);

    while (ring->next_to_clean != wb.cidx && work < real_count) {
        struct onic_tx_buffer *buf = &q->buffer[ring->next_to_clean];

        if (buf->type == ONIC_TX_SKB && buf->skb) {
            dma_unmap_single(&priv->pdev->dev, buf->dma_addr,
                             buf->len, DMA_TO_DEVICE);
            dev_kfree_skb_any(buf->skb);
        } else if (buf->type) {
            dev_warn_ratelimited(&priv->pdev->dev,
                                 "Unexpected VF TX buffer type=%u qid=%u\n",
                                 buf->type, q->qid);
        }

        memset(buf, 0, sizeof(*buf));
        onic_vf_ring_increment_clean(ring);
        work++;
    }
    if (work)
        dev_info_ratelimited(&priv->pdev->dev,
                            "VF TX cleaned: local_qid=%u count=%u cidx=%u\n",
                            q->qid, work, wb.cidx);
    if (ring->next_to_clean != wb.cidx)
        dev_warn_ratelimited(&priv->pdev->dev,
                             "Invalid VF TX writeback: qid=%u cidx=%u\n",
                             q->qid, wb.cidx);

    clear_bit(0, q->state);
}

static void onic_vf_release_pending_tx(struct onic_private *priv,
                                       struct onic_tx_queue *q)
{
    u16 real_count;
    u16 i;

    if (!q || !q->buffer)
        return;

    onic_vf_tx_clean(priv, q);
    real_count = onic_vf_ring_real_count(&q->ring);

    for (i = 0; i < real_count; i++) {
        struct onic_tx_buffer *buf = &q->buffer[i];

        if (buf->type != ONIC_TX_SKB || !buf->skb)
            continue;

        dma_unmap_single(&priv->pdev->dev, buf->dma_addr,
                         buf->len, DMA_TO_DEVICE);
        dev_kfree_skb_any(buf->skb);
        memset(buf, 0, sizeof(*buf));
    }
}

static void onic_vf_rx_refill_credit(struct onic_private *priv,
                                     struct onic_rx_queue *q)
{
    struct onic_ring *ring = &q->desc_ring;
    u16 real_count = onic_vf_ring_real_count(ring);
    u16 unused;

    unused = (ring->next_to_use + real_count - ring->next_to_clean) %
             real_count;
    if (unused >= ONIC_VF_RX_DESC_STEP / 2)
        return;

    ring->next_to_use = (ring->next_to_use + ONIC_VF_RX_DESC_STEP) %
                        real_count;
    dma_wmb();
    onic_vf_set_rx_head(priv, q->qid, ring->next_to_use);
}

static int onic_vf_rx_poll(struct napi_struct *napi, int budget)
{
    struct onic_rx_queue *q =
        container_of(napi, struct onic_rx_queue, napi);
    struct onic_private *priv = netdev_priv(q->netdev);
    struct onic_ring *desc_ring = &q->desc_ring;
    struct onic_ring *cmpl_ring = &q->cmpl_ring;
    struct qdma_c2h_cmpl_stat stat;
    int work = 0;
    struct rtnl_link_stats64 *stats = this_cpu_ptr(priv->netdev_stats);
    if (q->qid < priv->num_tx_queues)
        onic_vf_tx_clean(priv, READ_ONCE(priv->tx_queue[q->qid]));
    dma_rmb();
    qdma_unpack_c2h_cmpl_stat(&stat, cmpl_ring->wb);

    while (work < budget && cmpl_ring->next_to_clean != stat.pidx) {
        struct onic_rx_buffer *buf = &q->buffer[desc_ring->next_to_clean];
        struct qdma_c2h_st_desc desc;
        struct qdma_c2h_cmpl cmpl;
        struct sk_buff *skb;
        struct page *new_pg;
        u8 *cmpl_ptr;
        u8 *desc_ptr;

        cmpl_ptr = cmpl_ring->desc +
                   QDMA_C2H_CMPL_SIZE * cmpl_ring->next_to_clean;
        dma_rmb();
        qdma_unpack_c2h_cmpl(&cmpl, cmpl_ptr);

        if (cmpl.color != cmpl_ring->color || cmpl.err ||
            !cmpl.pkt_len ||
            cmpl.pkt_len > PAGE_SIZE - buf->offset) {
                dev_warn_ratelimited(&priv->pdev->dev,
                                    "Invalid VF RX completion: qid=%u len=%u err=%u color=%u expected=%u\n",
                                    q->qid, cmpl.pkt_len, cmpl.err,
                                    cmpl.color, cmpl_ring->color);
                stats->rx_errors++;
            break;
        }

        new_pg = page_pool_dev_alloc_pages(q->page_pool);
        if (!new_pg){
            stats->rx_dropped++;
            break;
        }

        dma_sync_single_for_cpu(&priv->pdev->dev,
                                page_pool_get_dma_addr(buf->pg) + buf->offset,
                                cmpl.pkt_len, DMA_FROM_DEVICE);

        skb = napi_build_skb(page_address(buf->pg), PAGE_SIZE);
        if (!skb) {
            page_pool_put_full_page(q->page_pool, new_pg, false);
            stats->rx_dropped++;
            break;
        }

        skb_mark_for_recycle(skb);
        skb_reserve(skb, buf->offset);
        skb_put(skb, cmpl.pkt_len);
        skb->protocol = eth_type_trans(skb, q->netdev);
        skb->ip_summed = CHECKSUM_NONE;

        buf->pg = new_pg;
        buf->offset = XDP_PACKET_HEADROOM;
        desc.dst_addr = page_pool_get_dma_addr(new_pg) + buf->offset;
        desc_ptr = desc_ring->desc +
                   QDMA_C2H_ST_DESC_SIZE * desc_ring->next_to_clean;
        qdma_pack_c2h_st_desc(desc_ptr, &desc);

        onic_vf_ring_increment_clean(desc_ring);
        onic_vf_ring_increment_clean(cmpl_ring);
        if (!cmpl_ring->next_to_clean)
            cmpl_ring->color ^= 1;

        stats->rx_packets++;
        stats->rx_bytes += cmpl.pkt_len;
        napi_gro_receive(napi, skb);
        work++;
        onic_vf_rx_refill_credit(priv, q);
    }

    dma_wmb();
    if (work < budget && napi_complete_done(napi, work))
        onic_vf_set_completion_tail(priv, q->qid,
                                    cmpl_ring->next_to_clean, 1);
    else
        onic_vf_set_completion_tail(priv, q->qid,
                                    cmpl_ring->next_to_clean, 0);

    return work;
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
    onic_vf_release_pending_tx(priv, q);
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

static int onic_vf_clear_one_tx_context(struct onic_private *priv, u16 qid)
{
    struct onic_tx_queue *q = priv->tx_queue[qid];
    int err;

    if (!q || !test_bit(ONIC_VF_TX_CTXT_CONFIGURED, q->state))
        return 0;

    err = onic_vf_mbox_clear_queue(priv, qid, ONIC_MBOX_QUEUE_DIR_TX);
    if (err)
        return err;

    clear_bit(ONIC_VF_TX_CTXT_CONFIGURED, q->state);

    dev_info(&priv->pdev->dev,
             "VF TX context cleared: local_qid=%u global_qid=%u\n",
             qid, priv->vf_hw.qbase + qid);

    return 0;
}

static int onic_vf_config_one_tx_context(struct onic_private *priv, u16 qid)
{
    struct onic_tx_queue *q = priv->tx_queue[qid];
    struct onic_mbox_queue_cfg cfg = {0};
    int err;

    if (!q || !q->vector)
        return -EINVAL;

    if (test_bit(ONIC_VF_TX_CTXT_CONFIGURED, q->state))
        return -EALREADY;

    cfg.qid = qid;
    cfg.dir = ONIC_MBOX_QUEUE_DIR_TX;
    cfg.vector = q->vector->vid;
    cfg.rngcnt_idx = ONIC_VF_TX_RNGCNT_IDX;
    cfg.desc_dma_lo = lower_32_bits(q->ring.dma_addr);
    cfg.desc_dma_hi = upper_32_bits(q->ring.dma_addr);

    err = onic_vf_mbox_config_queue(priv, &cfg);
    if (err)
        return err;

    set_bit(ONIC_VF_TX_CTXT_CONFIGURED, q->state);

    dev_info(&priv->pdev->dev,
             "VF TX context configured: local_qid=%u global_qid=%u vector=%u\n",
             qid, priv->vf_hw.qbase + qid, cfg.vector);

    return 0;
}

int onic_vf_tx_contexts_clear(struct onic_private *priv)
{
    int first_err = 0;
    int err;
    u16 qid;

    for (qid = 0; qid < priv->num_tx_queues; qid++) {
        err = onic_vf_clear_one_tx_context(priv, qid);
        if (err && !first_err)
            first_err = err;
    }

    return first_err;
}

int onic_vf_tx_contexts_init(struct onic_private *priv)
{
    u16 qid;
    int err;

    for (qid = 0; qid < priv->num_tx_queues; qid++) {
        err = onic_vf_config_one_tx_context(priv, qid);
        if (err)
            goto err_clear;
    }

    return 0;

err_clear:
    onic_vf_tx_contexts_clear(priv);
    return err;
}

static int onic_vf_clear_one_rx_context(struct onic_private *priv, u16 qid)
{
    struct onic_rx_queue *q = priv->rx_queue[qid];
    int err;

    if (!q || !test_bit(ONIC_VF_RX_CTXT_CONFIGURED, q->state))
        return 0;

    err = onic_vf_mbox_clear_queue(priv, qid, ONIC_MBOX_QUEUE_DIR_RX);
    if (err)
        return err;

    clear_bit(ONIC_VF_RX_CTXT_CONFIGURED, q->state);

    dev_info(&priv->pdev->dev,
             "VF RX context cleared: local_qid=%u global_qid=%u\n",
             qid, priv->vf_hw.qbase + qid);

    return 0;
}

static int onic_vf_config_one_rx_context(struct onic_private *priv, u16 qid)
{
    struct onic_rx_queue *q = priv->rx_queue[qid];
    struct onic_mbox_queue_cfg cfg = {0};
    int err;

    if (!q || !q->vector || !q->desc_ring.desc || !q->cmpl_ring.desc)
        return -EINVAL;

    if (test_bit(ONIC_VF_RX_CTXT_CONFIGURED, q->state))
        return -EALREADY;

    cfg.qid = qid;
    cfg.dir = ONIC_MBOX_QUEUE_DIR_RX;
    cfg.vector = q->vector->vid;
    cfg.rngcnt_idx = ONIC_VF_RX_DESC_RNGCNT_IDX;
    cfg.bufsz_idx = ONIC_VF_RX_BUFSZ_IDX;
    cfg.cmpl_rngcnt_idx = ONIC_VF_RX_CMPL_RNGCNT_IDX;
    cfg.cmpl_desc_sz = ONIC_VF_RX_CMPL_DESC_SZ;
    cfg.desc_dma_lo = lower_32_bits(q->desc_ring.dma_addr);
    cfg.desc_dma_hi = upper_32_bits(q->desc_ring.dma_addr);
    cfg.cmpl_dma_lo = lower_32_bits(q->cmpl_ring.dma_addr);
    cfg.cmpl_dma_hi = upper_32_bits(q->cmpl_ring.dma_addr);

    err = onic_vf_mbox_config_queue(priv, &cfg);
    if (err)
        return err;

    set_bit(ONIC_VF_RX_CTXT_CONFIGURED, q->state);

    dev_info(&priv->pdev->dev,
             "VF RX context configured: local_qid=%u global_qid=%u vector=%u\n",
             qid, priv->vf_hw.qbase + qid, cfg.vector);

    return 0;
}

int onic_vf_rx_contexts_clear(struct onic_private *priv)
{
    int first_err = 0;
    int err;
    u16 qid;

    for (qid = 0; qid < priv->num_rx_queues; qid++) {
        err = onic_vf_clear_one_rx_context(priv, qid);
        if (err && !first_err)
            first_err = err;
    }

    return first_err;
}

int onic_vf_rx_contexts_init(struct onic_private *priv)
{
    u16 qid;
    int err;

    for (qid = 0; qid < priv->num_rx_queues; qid++) {
        err = onic_vf_config_one_rx_context(priv, qid);
        if (err)
            goto err_clear;
    }

    return 0;

err_clear:
    onic_vf_rx_contexts_clear(priv);
    return err;
}
void onic_vf_rx_datapath_clear(struct onic_private *priv)
{
    u16 qid;

    for (qid = 0; qid < priv->num_rx_queues; qid++) {
        struct onic_rx_queue *q = priv->rx_queue[qid];

        if (!q || !test_bit(ONIC_VF_RX_NAPI_ENABLED, q->state))
            continue;

        onic_vf_set_completion_tail(priv, qid,
                                    q->cmpl_ring.next_to_clean, 0);
        clear_bit(ONIC_VF_RX_NAPI_ENABLED, q->state);
        synchronize_irq(pci_irq_vector(priv->pdev, q->vector->vid));
        napi_disable(&q->napi);
        netif_napi_del(&q->napi);

        dev_info(&priv->pdev->dev,
                 "VF RX datapath stopped: local_qid=%u\n", qid);
    }
}

int onic_vf_rx_datapath_init(struct onic_private *priv)
{
    u16 qid;
    int err = -EINVAL;

    for (qid = 0; qid < priv->num_rx_queues; qid++) {
        struct onic_rx_queue *q = priv->rx_queue[qid];

        if (!q || !test_bit(ONIC_VF_RX_CTXT_CONFIGURED, q->state) ||
            ONIC_VF_RX_DESC_STEP >= onic_vf_ring_real_count(&q->desc_ring))
            goto err_clear;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
        netif_napi_add(priv->netdev, &q->napi, onic_vf_rx_poll);
#else
        netif_napi_add(priv->netdev, &q->napi, onic_vf_rx_poll, 64);
#endif
        napi_enable(&q->napi);
        set_bit(ONIC_VF_RX_NAPI_ENABLED, q->state);

        q->desc_ring.next_to_use = ONIC_VF_RX_DESC_STEP;
        dma_wmb();
        onic_vf_set_rx_head(priv, qid, q->desc_ring.next_to_use);
        onic_vf_set_completion_tail(priv, qid, 0, 1);

        dev_info(&priv->pdev->dev,
                 "VF RX datapath started: local_qid=%u advertised=%u\n",
                 qid, q->desc_ring.next_to_use);
    }

    return 0;

err_clear:
    onic_vf_rx_datapath_clear(priv);
    return err;
}

static int onic_vf_create_rx_page_pool(struct onic_private *priv,
                                       struct onic_rx_queue *q, u16 size)
{
    struct page_pool_params params = {
        .order = 0,
        .flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
        .pool_size = size,
        .nid = dev_to_node(&priv->pdev->dev),
        .dev = &priv->pdev->dev,
        .dma_dir = DMA_FROM_DEVICE,
        .offset = XDP_PACKET_HEADROOM,
        .max_len = priv->netdev->mtu + ETH_HLEN,
    };
    int err;

    q->page_pool = page_pool_create(&params);
    if (IS_ERR(q->page_pool)) {
        err = PTR_ERR(q->page_pool);
        q->page_pool = NULL;
        return err;
    }

    return 0;
}

static void onic_vf_free_rx_pages(struct onic_rx_queue *q)
{
    u16 real_count;
    int i;

    if (!q->page_pool)
        return;

    real_count = q->desc_ring.count - 1;

    if (q->buffer) {
        for (i = 0; i < real_count; i++) {
            if (!q->buffer[i].pg)
                continue;

            page_pool_put_full_page(q->page_pool, q->buffer[i].pg, false);
            q->buffer[i].pg = NULL;
        }
    }

    page_pool_destroy(q->page_pool);
    q->page_pool = NULL;
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
    onic_vf_free_rx_pages(q);
    kfree(q->buffer);
    kfree(q);
    priv->rx_queue[qid] = NULL;
}

static int onic_vf_alloc_rx_ring(struct onic_private *priv, u16 qid)
{
    struct onic_rx_queue *q;
    size_t size;
    u16 real_count;
    int i;
    int err = -ENOMEM;

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

    err = onic_vf_create_rx_page_pool(priv, q, real_count);
    if (err)
        goto err_free_buffer;

    for (i = 0; i < real_count; i++) {
        struct qdma_c2h_st_desc desc;
        struct page *pg;
        u8 *desc_ptr;

        pg = page_pool_dev_alloc_pages(q->page_pool);
        if (!pg) {
            err = -ENOMEM;
            goto err_free_pages;
        }

        q->buffer[i].pg = pg;
        q->buffer[i].offset = XDP_PACKET_HEADROOM;

        desc.dst_addr = page_pool_get_dma_addr(pg) + XDP_PACKET_HEADROOM;
        desc_ptr = q->desc_ring.desc + QDMA_C2H_ST_DESC_SIZE * i;
        qdma_pack_c2h_st_desc(desc_ptr, &desc);
    }

    dev_info(&priv->pdev->dev,
             "VF RX descriptors prepared: local_qid=%u pages=%u\n",
             qid, real_count);

    q->cmpl_ring.count = ONIC_VF_CMPL_RING_COUNT;
    size = onic_vf_ring_bytes(q->cmpl_ring.count, QDMA_C2H_CMPL_SIZE,
                              QDMA_C2H_CMPL_STAT_SIZE);

    q->cmpl_ring.desc = dma_alloc_coherent(&priv->pdev->dev, size,
                                           &q->cmpl_ring.dma_addr,
                                           GFP_KERNEL);
    if (!q->cmpl_ring.desc)
        goto err_free_pages;

    memset(q->cmpl_ring.desc, 0, size);
    q->cmpl_ring.wb =
        q->cmpl_ring.desc +
        QDMA_C2H_CMPL_SIZE * (q->cmpl_ring.count - 1);
    q->cmpl_ring.color = 1;

    priv->rx_queue[qid] = q;
    return 0;

err_free_pages:
    onic_vf_free_rx_pages(q);
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
    
    return err;
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