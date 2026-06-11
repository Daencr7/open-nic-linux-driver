#ifndef _ONIC_VF_MBOX_H_
#define _ONIC_VF_MBOX_H_
#include <linux/types.h>
#include <linux/dma-mapping.h>
struct onic_private;

int onic_vf_mbox_process_one(struct onic_private *priv);
int onic_vf_mbox_get_queue_resource(struct onic_private *priv);
int onic_vf_mbox_irq_init(struct onic_private *priv, u16 vector);
void onic_vf_mbox_irq_clear(struct onic_private *priv);
int onic_vf_mbox_init_tx_queue(struct onic_private *priv, u16 local_qid,
			       dma_addr_t desc_dma_addr, u8 rngcnt_idx,
			       u16 vector);
#endif /* _ONIC_VF_MBOX_H_ */