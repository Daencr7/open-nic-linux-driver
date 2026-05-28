/* data structures for VF */
#ifndef __ONIC_VF_H__
#define __ONIC_VF_H__   

struct onic_vf_resource {
	u16 vf_id;
	u16 func_id;

	u16 qbase;
	u16 qmax;

	u16 num_tx_queues;
	u16 num_rx_queues;

	u8 mac[ETH_ALEN];

	bool enabled;
};


#endif /* __ONIC_VF_H__ */