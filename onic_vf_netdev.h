/*
* 
*
*/


#ifndef ONIC_VF_NETDEV_H
#define ONIC_VF_NETDEV_H
#include <linux/netdevice.h>

static int onic_vf_open_netdev(struct net_device *netdev);

static int onic_vf_stop_netdev(struct net_device *netdev);

static netdev_tx_t onic_vf_xmit_frame(struct sk_buff *skb,
				      struct net_device *netdev);


#endif /* ONIC_VF_NETDEV_H */