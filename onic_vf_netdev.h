/*
* 
*
*/


#ifndef ONIC_VF_NETDEV_H
#define ONIC_VF_NETDEV_H
#include <linux/netdevice.h>
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


#endif /* ONIC_VF_NETDEV_H */