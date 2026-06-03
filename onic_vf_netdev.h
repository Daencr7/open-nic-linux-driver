/*
* 
*
*/


#ifndef ONIC_VF_NETDEV_H
#define ONIC_VF_NETDEV_H
#include <linux/netdevice.h>

struct onic_private;

/**
 * This header defines the netdev operations for the OpenNIC VF driver.
 * @param netdev The net_device structure representing the VF netdev
 * @return 0 on success, negative on failure
 */
int onic_vf_open_netdev(struct net_device *netdev);

/**
 * Stop the VF netdev
 * @param netdev The net_device structure representing the VF netdev
 * @return 0 on success, negative on failure
 */
int onic_vf_stop_netdev(struct net_device *netdev);

/**
 * Transmit a packet on the VF netdev. This is a dummy implementation that just drops the packet.
 * @param skb The socket buffer containing the packet to transmit
 * @param netdev The net_device structure representing the VF netdev
 * @return NETDEV_TX_OK to indicate that the packet was "transmitted" (dropped in this case)
 */
netdev_tx_t onic_vf_xmit_frame(struct sk_buff *skb,
				      struct net_device *netdev);


void onic_vf_get_stats64(struct net_device *netdev,
	                         struct rtnl_link_stats64 *stats);
int onic_vf_set_mac_address(struct net_device *netdev, void *addr);
int onic_vf_change_mtu(struct net_device *netdev, int mtu);

void onic_vf_tx_clean_work_init(struct onic_private *priv);
void onic_vf_tx_clean_work_start(struct onic_private *priv);
void onic_vf_tx_clean_work_stop(struct onic_private *priv);

#endif /* ONIC_VF_NETDEV_H */
