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
int onic_vf_mbox_clear_tx_queue(struct onic_private *priv, u16 local_qid);

int onic_vf_mbox_init_rx_queue(struct onic_private *priv, u16 local_qid,
			       dma_addr_t desc_dma_addr,
			       dma_addr_t cmpl_dma_addr,
			       u8 desc_rngcnt_idx,
			       u8 cmpl_rngcnt_idx,
			       u8 bufsz_idx,
			       u8 cmpl_desc_sz,
			       u16 vector);

int onic_vf_mbox_clear_rx_queue(struct onic_private *priv, u16 local_qid);
#endif /* _ONIC_VF_MBOX_H_ */