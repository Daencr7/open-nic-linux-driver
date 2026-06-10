#ifndef __ONIC_VF_QDMA_H__
#define __ONIC_VF_QDMA_H__
#include <linux/netdevice.h>

struct onic_private;

int onic_vf_qdma_init(struct onic_private *priv);
void onic_vf_qdma_clear(struct onic_private *priv);

int onic_vf_rings_init(struct onic_private *priv);
void onic_vf_rings_clear(struct onic_private *priv);



netdev_tx_t onic_vf_qdma_xmit_frame(struct sk_buff *skb,
				     struct net_device *netdev);

#endif