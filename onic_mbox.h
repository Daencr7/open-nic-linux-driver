#ifndef __ONIC_MBOX_H__
#define __ONIC_MBOX_H__
					
#include <linux/types.h>

#define ONIC_MBOX_MSG_SIZE             128
#define ONIC_MBOX_MAX_PAYLOAD          112

#define ONIC_MBOX_OP_GET_QUEUE_RES     0x01
#define ONIC_MBOX_OP_QUEUE_RES_RESP    0x02

#define ONIC_MBOX_STS_OK               0x00
#define ONIC_MBOX_STS_ERR              0x01

struct onic_mbox_hdr {
	u32 opcode;
	u32 status;
	u32 seq;
	u32 len;
};

struct onic_mbox_queue_res {
	u32 func_id;
	u32 qbase;
	u32 qmax;
};

struct onic_mbox_msg {
	struct onic_mbox_hdr hdr;

	union {
		struct onic_mbox_queue_res qres;
		u8 raw[ONIC_MBOX_MAX_PAYLOAD];
	} data;
};
struct onic_private;

int onic_pf_mbox_process_one(struct onic_private *priv);

int onic_pf_mbox_process_pending(struct onic_private *priv);
int onic_pf_mbox_irq_init(struct onic_private *priv, u16 vector);
void onic_pf_mbox_irq_enable(struct onic_private *priv);
void onic_pf_mbox_irq_disable(struct onic_private *priv);
#endif /* __ONIC_MBOX_H__ */