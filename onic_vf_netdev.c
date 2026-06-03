/** 
 * This netdev ops just for VF
 * - open VF netdev
 * - stop VF netdev
 * - transmit packet
 * - change MAC
 * - change MTU
 * - get stats
 * 
 * 
 * Đây là phần Linux network interface của VF.
 * 
- ndo_open
- ndo_stop
- ndo_start_xmit
- ndo_get_stats64
- NAPI poll
- RX refill
- TX clean
Nhưng giai đoạn đầu có thể để tối giản:
open()  → return 0
stop()  → return 0
xmit()  → dev_kfree_skb(); return NETDEV_TX_OK
Mục tiêu đầu tiên chỉ là:
ip link thấy VF netdev
ip link set dev <vf> up không crash
Sau đó mới nối datapath thật.

 */
#include <linux/if_link.h>
#include <linux/pci_regs.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/bpf_trace.h>
#include <linux/percpu.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <net/page_pool/helpers.h>
#include <net/page_pool/types.h>
#else 
#include <net/page_pool.h>
#endif


#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include "qdma_export.h"
#include "onic_vf_netdev.h"
#include "onic.h"
#include "onic_vf_qdma.h"


#define ONIC_VF_TX_CLEAN_INTERVAL_MS 100
/**
 * Need to developemt
 * - open VF netdev
 */
int onic_vf_open_netdev(struct net_device *netdev)
{
    struct onic_private *priv = netdev_priv(netdev);
    int err;

    netdev_info(netdev, "onic_vf_open called\n");

    err = onic_vf_rings_init(priv);
    if (err) {
        netdev_err(netdev, "Failed to allocate inactive rings: %d\n",
                   err);
        return err;
    }

    err = onic_vf_tx_contexts_init(priv);
    if (err) {
        netdev_err(netdev, "Failed to configure VF TX contexts: %d\n",
                   err);
        onic_vf_rings_clear(priv);
        return err;
    }

    err = onic_vf_rx_contexts_init(priv);
    if (err) {
        netdev_err(netdev, "Failed to configure VF RX contexts: %d\n",
                   err);
        onic_vf_tx_contexts_clear(priv);
        onic_vf_rings_clear(priv);
        return err;
    }

    err = onic_vf_rx_datapath_init(priv);
    if (err) {
        netdev_err(netdev, "Failed to start VF RX datapath: %d\n", err);
        onic_vf_rx_contexts_clear(priv);
        onic_vf_tx_contexts_clear(priv);
        onic_vf_rings_clear(priv);
        return err;
    }

	netif_tx_start_all_queues(netdev);
	netif_carrier_on(netdev);
	onic_vf_tx_clean_work_start(priv);
	onic_vf_rx_poll_work_start(priv);

	return 0;
}
/** 
 * Need to developemt
 * - stop VF netdev
 */
int onic_vf_stop_netdev(struct net_device *netdev)
{
    struct onic_private *priv = netdev_priv(netdev);
    int err;
    netdev_info(netdev, "onic_vf_stop called\n");

	netif_carrier_off(netdev);

	netif_tx_stop_all_queues(netdev);
	onic_vf_rx_poll_work_stop(priv);
	onic_vf_tx_clean_work_stop(priv);
	onic_vf_rx_datapath_clear(priv);
    err = onic_vf_rx_contexts_clear(priv);
    if (err) {
        netdev_err(netdev,
                   "Failed to clear VF RX contexts, rings retained: %d\n",
                   err);
        return err;
    }

    err = onic_vf_tx_contexts_clear(priv);
    if (err) {
        netdev_err(netdev,
                   "Failed to clear VF TX contexts, rings retained: %d\n",
                   err);
        return err;
    }

    onic_vf_rings_clear(priv);

    return 0;
}

void onic_vf_get_stats64(struct net_device *netdev,
                         struct rtnl_link_stats64 *stats)
{
    struct onic_private *priv = netdev_priv(netdev);
    struct rtnl_link_stats64 *pcpu;
    unsigned int cpu;

    memset(stats, 0, sizeof(*stats));

    for_each_possible_cpu(cpu) {
        pcpu = per_cpu_ptr(priv->netdev_stats, cpu);
        stats->rx_packets += pcpu->rx_packets;
        stats->rx_bytes += pcpu->rx_bytes;
        stats->rx_errors += pcpu->rx_errors;
        stats->rx_dropped += pcpu->rx_dropped;
        stats->tx_packets += pcpu->tx_packets;
        stats->tx_bytes += pcpu->tx_bytes;
        stats->tx_errors += pcpu->tx_errors;
        stats->tx_dropped += pcpu->tx_dropped;
	}
}

int onic_vf_set_mac_address(struct net_device *netdev, void *addr)
{
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, saddr->sa_data);
	netdev_info(netdev, "Set VF MAC address to %pM\n", netdev->dev_addr);

	return 0;
}

int onic_vf_change_mtu(struct net_device *netdev, int mtu)
{
	if (mtu < netdev->min_mtu || mtu > netdev->max_mtu)
		return -EINVAL;

	netdev->mtu = mtu;
	netdev_info(netdev, "Set VF MTU to %d\n", mtu);

	return 0;
}

static u16 onic_vf_tx_ring_real_count(const struct onic_ring *ring)
{
    return ring->count - 1;
}

static bool onic_vf_tx_ring_full(const struct onic_ring *ring)
{
    u16 next = ring->next_to_use + 1;

    if (next == onic_vf_tx_ring_real_count(ring))
        next = 0;

    return next == ring->next_to_clean;
}

static void onic_vf_tx_ring_increment_head(struct onic_ring *ring)
{
    ring->next_to_use++;

    if (ring->next_to_use == onic_vf_tx_ring_real_count(ring))
        ring->next_to_use = 0;
}

static void onic_vf_tx_maybe_wake_subqueue(struct net_device *netdev,
                                           u16 qid,
                                           struct onic_tx_queue *q)
{
    struct netdev_queue *txq;

    if (!q)
        return;

    txq = netdev_get_tx_queue(netdev, qid);
    if (netif_tx_queue_stopped(txq) && !onic_vf_tx_ring_full(&q->ring))
        netif_wake_subqueue(netdev, qid);
}

static void onic_vf_tx_clean_work_fn(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct onic_private *priv =
        container_of(dwork, struct onic_private, vf_tx_clean_work);
    struct net_device *netdev = priv->netdev;
    u16 qid;

    if (!netdev || !netif_running(netdev))
        return;

    for (qid = 0; qid < priv->num_tx_queues; qid++) {
        struct onic_tx_queue *q = READ_ONCE(priv->tx_queue[qid]);

        if (!q)
            continue;

        onic_vf_tx_clean(priv, q);
        onic_vf_tx_maybe_wake_subqueue(netdev, qid, q);
    }

    schedule_delayed_work(&priv->vf_tx_clean_work,
                          msecs_to_jiffies(ONIC_VF_TX_CLEAN_INTERVAL_MS));
}

void onic_vf_tx_clean_work_init(struct onic_private *priv)
{
    INIT_DELAYED_WORK(&priv->vf_tx_clean_work, onic_vf_tx_clean_work_fn);
}

void onic_vf_tx_clean_work_start(struct onic_private *priv)
{
    schedule_delayed_work(&priv->vf_tx_clean_work,
                          msecs_to_jiffies(ONIC_VF_TX_CLEAN_INTERVAL_MS));
}

void onic_vf_tx_clean_work_stop(struct onic_private *priv)
{
    cancel_delayed_work_sync(&priv->vf_tx_clean_work);
}

/**
 * Need to developemt
 * - transmit packet
 */
netdev_tx_t onic_vf_xmit_frame(struct sk_buff *skb,
                               struct net_device *netdev)
{
    struct onic_private *priv = netdev_priv(netdev);
    struct rtnl_link_stats64 *stats = this_cpu_ptr(priv->netdev_stats);
    struct onic_tx_queue *q;
    struct onic_ring *ring;
    struct qdma_h2c_st_desc desc;
    dma_addr_t dma_addr;
    u8 *desc_ptr;
    u16 qid;

    if (unlikely(!priv->num_tx_queues)) {
        stats->tx_dropped++;
        stats->tx_errors++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    qid = skb_get_queue_mapping(skb) % priv->num_tx_queues;
    q = READ_ONCE(priv->tx_queue[qid]);

    if (unlikely(!q || !q->ring.desc)) {
        stats->tx_dropped++;
        stats->tx_errors++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    ring = &q->ring;
    onic_vf_tx_clean(priv, q);

	if (unlikely(onic_vf_tx_ring_full(ring))) {
		netif_stop_subqueue(netdev, qid);
		onic_vf_tx_clean(priv, q);

		if (onic_vf_tx_ring_full(ring))
			return NETDEV_TX_BUSY;

		netif_wake_subqueue(netdev, qid);
	}

    if (unlikely(skb_put_padto(skb, ETH_ZLEN))) {
        stats->tx_dropped++;
        stats->tx_errors++;
        return NETDEV_TX_OK;
    }

    dma_addr = dma_map_single(&priv->pdev->dev, skb->data, skb->len,
                              DMA_TO_DEVICE);
    if (unlikely(dma_mapping_error(&priv->pdev->dev, dma_addr))) {
        stats->tx_dropped++;
        stats->tx_errors++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    desc.metadata = skb->len;
    desc.len = skb->len;
    desc.src_addr = dma_addr;

    desc_ptr = ring->desc + QDMA_H2C_ST_DESC_SIZE * ring->next_to_use;
    qdma_pack_h2c_st_desc(desc_ptr, &desc);

    q->buffer[ring->next_to_use].type = ONIC_TX_SKB;
    q->buffer[ring->next_to_use].skb = skb;
    q->buffer[ring->next_to_use].dma_addr = dma_addr;
    q->buffer[ring->next_to_use].len = skb->len;

    stats->tx_packets++;
    stats->tx_bytes += skb->len;

    onic_vf_tx_ring_increment_head(ring);

	dma_wmb();
	onic_vf_set_tx_head(priv, qid, ring->next_to_use);

	if (onic_vf_tx_ring_full(ring))
		netif_stop_subqueue(netdev, qid);

	dev_info_ratelimited(&priv->pdev->dev,
                        "VF TX submitted: local_qid=%u pidx=%u len=%u\n",
                        qid, ring->next_to_use, desc.len);

    return NETDEV_TX_OK;
}
