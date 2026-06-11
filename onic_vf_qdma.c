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

#include "onic.h"
#include "onic_vf_qdma.h"
#include "qdma_device.h"
#include "onic_common.h"
#include "onic_hardware.h"
#include "qdma_register.h"
#include "onic_vf_mbox.h"

#define ONIC_VF_TX_RNGCNT_IDX       0
#define ONIC_VF_RX_DESC_RNGCNT_IDX  8
#define ONIC_VF_RX_CMPL_RNGCNT_IDX  8
/* Helper function */
static inline u16 onic_vf_global_qid(struct onic_private *priv, u16 local_qid)
{
	return priv->vf_hw.qbase + local_qid;
}

static void onic_vf_set_tx_head(struct onic_private *priv, u16 qid, u16 head)
{
    u32 val;

    val = FIELD_SET(QDMA_DMAP_SEL_DESC_PIDX_MASK, head);
    onic_vf_write_bar0(priv,
        QDMA_OFFSET_VF_DMAP_SEL_H2C_DESC_PIDX + qid * 16,
        val);
}
static inline u16 onic_vf_ring_real_count(struct onic_ring *ring)
{
	return ring->wb ? ring->count - 1 : ring->count;
}

static inline bool onic_vf_ring_full(struct onic_ring *ring)
{
	u16 real_count = onic_vf_ring_real_count(ring);

	return ((ring->next_to_use + 1) % real_count) == ring->next_to_clean;
}

static inline void onic_vf_ring_increment_head(struct onic_ring *ring)
{
	ring->next_to_use = (ring->next_to_use + 1) %
			    onic_vf_ring_real_count(ring);
}

static inline void onic_vf_ring_increment_tail(struct onic_ring *ring)
{
	ring->next_to_clean = (ring->next_to_clean + 1) %
			      onic_vf_ring_real_count(ring);
}
static void onic_vf_tx_clean(struct onic_tx_queue *q)
{
	struct onic_private *priv;
	struct onic_ring *ring;
	struct qdma_wb_stat wb;
	int work, i;

	if (!q || !q->netdev)
		return;

	priv = netdev_priv(q->netdev);
	ring = &q->ring;

	if (!ring->wb || !q->buffer)
		return;

	if (test_and_set_bit(0, q->state))
		return;

	qdma_unpack_wb_stat(&wb, ring->wb);

	if (wb.cidx == ring->next_to_clean)
		goto out;

	work = wb.cidx - ring->next_to_clean;
	if (work < 0)
		work += onic_vf_ring_real_count(ring);

	for (i = 0; i < work; i++) {
		struct onic_tx_buffer *buf = &q->buffer[ring->next_to_clean];

		if (buf->type == ONIC_TX_SKB && buf->skb) {
			dma_unmap_single(&priv->pdev->dev, buf->dma_addr,
					 buf->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(buf->skb);
			memset(buf, 0, sizeof(*buf));
		}

		onic_vf_ring_increment_tail(ring);
	}

out:
	clear_bit(0, q->state);
}
/*  main function */
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

	priv->num_tx_queues = vf_hw->num_tx_queues;
	priv->num_rx_queues = vf_hw->num_rx_queues;

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

	priv->num_tx_queues = 0;
	priv->num_rx_queues = 0;

	vf_hw->resource_valid = false;
}

static int onic_vf_init_tx_ring(struct onic_private *priv, u16 qid)
{
	struct onic_tx_queue *q;
	struct onic_ring *ring;
	// struct onic_qdma_h2c_param param;
	u32 real_count, size;
	u16 vid;
	int rv;

	if (!priv->vf_hw.qdev)
		return -ENODEV;

	if (qid >= priv->num_tx_queues)
		return -EINVAL;

	if (priv->tx_queue[qid])
		return 0;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return -ENOMEM;

	q->netdev = priv->netdev;
	q->qid = qid;

	ring = &q->ring;
	ring->count = onic_ring_count(ONIC_VF_TX_RNGCNT_IDX);
	real_count = ring->count - 1;

	size = QDMA_H2C_ST_DESC_SIZE * real_count + QDMA_WB_STAT_SIZE;
	size = ALIGN(size, PAGE_SIZE);

	ring->desc = dma_alloc_coherent(&priv->pdev->dev, size,
					&ring->dma_addr, GFP_KERNEL);
	if (!ring->desc) {
		rv = -ENOMEM;
		goto err_free_q;
	}

	memset(ring->desc, 0, size);
	ring->wb = ring->desc + QDMA_H2C_ST_DESC_SIZE * real_count;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;
	ring->color = 0;

	q->buffer = kcalloc(real_count, sizeof(struct onic_tx_buffer), GFP_KERNEL);
	if (!q->buffer) {
		rv = -ENOMEM;
		goto err_free_dma;
	}

	vid = priv->num_q_vectors ? 1 + (qid % priv->num_q_vectors) : 0;

	// param.rngcnt_idx = ONIC_VF_TX_RNGCNT_IDX;
	// param.dma_addr = ring->dma_addr;
	// param.vid = vid;
	// tam thoi cho VF tự init queue, chưa cần PF init hộ
	vid = 0;

	rv = onic_vf_mbox_init_tx_queue(priv, qid, ring->dma_addr,
					ONIC_VF_TX_RNGCNT_IDX, vid);
	if (rv)
		goto err_free_buf;

	priv->tx_queue[qid] = q;

	netdev_info(priv->netdev,
		    "VF TX ring init: local_qid=%u global_qid=%u vid=%u count=%u\n",
		    qid, onic_vf_global_qid(priv, qid), vid, ring->count);

	return 0;

err_free_buf:
	kfree(q->buffer);
err_free_dma:
	dma_free_coherent(&priv->pdev->dev, size, ring->desc, ring->dma_addr);
err_free_q:
	kfree(q);
	return rv;
}

static void onic_vf_clear_tx_ring(struct onic_private *priv, u16 qid)
{
	struct onic_tx_queue *q = priv->tx_queue[qid];
	struct onic_ring *ring;
	u32 real_count, size;
	int i;

	if (!q)
		return;

	onic_vf_tx_clean(q);

	if (priv->vf_hw.resource_valid) {
		int rv = onic_vf_mbox_clear_tx_queue(priv, qid);

		if (rv)
			netdev_warn(priv->netdev,
					"VF TXQ context clear failed: qid=%u err=%d\n",
					qid, rv);
	}

	ring = &q->ring;
	real_count = ring->count - 1;
	size = QDMA_H2C_ST_DESC_SIZE * real_count + QDMA_WB_STAT_SIZE;
	size = ALIGN(size, PAGE_SIZE);

	for (i = 0; q->buffer && i < real_count; i++) {
		struct onic_tx_buffer *buf = &q->buffer[i];

		if (buf->type == ONIC_TX_SKB && buf->skb) {
			dma_unmap_single(&priv->pdev->dev, buf->dma_addr,
					 buf->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(buf->skb);
		}
	}

	if (ring->desc)
		dma_free_coherent(&priv->pdev->dev, size,
				  ring->desc, ring->dma_addr);

	kfree(q->buffer);
	kfree(q);
	priv->tx_queue[qid] = NULL;
}

int onic_vf_rings_init(struct onic_private *priv)
{
	int qid, err;

	for (qid = 0; qid < priv->num_tx_queues; qid++) {
		err = onic_vf_init_tx_ring(priv, qid);
		if (err)
			goto err_clear;
	}

	return 0;

err_clear:
	while (qid--)
		onic_vf_clear_tx_ring(priv, qid);

	return err;
}

void onic_vf_rings_clear(struct onic_private *priv)
{
	int qid;

	for (qid = 0; qid < priv->num_tx_queues; qid++)
		onic_vf_clear_tx_ring(priv, qid);
}

netdev_tx_t onic_vf_qdma_xmit_frame(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct onic_private *priv = netdev_priv(netdev);
	struct onic_tx_queue *q;
	struct onic_ring *ring;
	struct qdma_h2c_st_desc desc;
	dma_addr_t dma_addr;
	u8 *desc_ptr;
	u16 qid;
	int err;

	if (!priv->num_tx_queues) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	qid = skb_get_queue_mapping(skb);
	if (qid >= priv->num_tx_queues)
		qid %= priv->num_tx_queues;

	q = priv->tx_queue[qid];
	if (!q || !q->buffer) {
		dev_kfree_skb_any(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	ring = &q->ring;

	onic_vf_tx_clean(q);

	if (onic_vf_ring_full(ring))
		return NETDEV_TX_BUSY;

	err = skb_put_padto(skb, ETH_ZLEN);
	if (err) {
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	dma_addr = dma_map_single(&priv->pdev->dev, skb->data, skb->len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(&priv->pdev->dev, dma_addr)) {
		dev_kfree_skb_any(skb);
		netdev->stats.tx_dropped++;
		netdev->stats.tx_errors++;
		return NETDEV_TX_OK;
	}

	desc_ptr = ring->desc + QDMA_H2C_ST_DESC_SIZE * ring->next_to_use;

	desc.len = skb->len;
	desc.src_addr = dma_addr;
	desc.metadata = skb->len;
	qdma_pack_h2c_st_desc(desc_ptr, &desc);

	q->buffer[ring->next_to_use].type = ONIC_TX_SKB;
	q->buffer[ring->next_to_use].skb = skb;
	q->buffer[ring->next_to_use].dma_addr = dma_addr;
	q->buffer[ring->next_to_use].len = skb->len;

	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += skb->len;

	onic_vf_ring_increment_head(ring);

	if (onic_vf_ring_full(ring) || !netdev_xmit_more()) {
		wmb();
		onic_vf_set_tx_head(priv, qid, ring->next_to_use);
		
		// if ((netdev->stats.tx_packets & 0x3f) == 0) {
			struct qdma_wb_stat wb;

			memset(&wb, 0, sizeof(wb));
			if (ring->wb)
				qdma_unpack_wb_stat(&wb, ring->wb);

			netdev_info(netdev,
					"VF TX debug: qid=%u pidx=%u sw_cidx=%u wb_cidx=%u len=%u\n",
					qid, ring->next_to_use, ring->next_to_clean,
					wb.cidx, skb->len);
		// }
	}

	return NETDEV_TX_OK;
}