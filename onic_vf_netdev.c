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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <net/page_pool/helpers.h>
#include <net/page_pool/types.h>
#else 
#include <net/page_pool.h>
#endif

// #include "onic_netdev.h"
// #include "onic_hardware.h"
// #include "qdma_access/qdma_register.h"
// #include "onic.h"
#include "onic_vf_netdev.h"

/**
 * Need to developemt
 * - open VF netdev
 */
int onic_vf_open_netdev(struct net_device *netdev)
{
	netdev_info(netdev, "onic_vf_open called\n");

	netif_start_queue(netdev);
	netif_carrier_on(netdev);
	return 0;
}
/** 
 * Need to developemt
 * - stop VF netdev
 */
int onic_vf_stop_netdev(struct net_device *netdev)
{
	netdev_info(netdev, "onic_vf_stop called\n");

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	return 0;
}

/**
 * Need to developemt
 * - transmit packet
 */
netdev_tx_t onic_vf_xmit_frame(struct sk_buff *skb,
				      struct net_device *netdev)
{
	netdev_info(netdev, "VF dummy TX packet len=%u, drop\n", skb->len);

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}