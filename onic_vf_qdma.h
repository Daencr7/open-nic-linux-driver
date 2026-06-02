#ifndef __ONIC_VF_QDMA_H__
#define __ONIC_VF_QDMA_H__

struct onic_private;

int onic_vf_qdma_init(struct onic_private *priv);
void onic_vf_qdma_clear(struct onic_private *priv);
int onic_vf_q_irq_init(struct onic_private *priv);
void onic_vf_q_irq_clear(struct onic_private *priv);


void onic_vf_set_tx_head(struct onic_private *priv, u16 qid, u16 head);
void onic_vf_set_rx_head(struct onic_private *priv, u16 qid, u16 head);
void onic_vf_set_completion_tail(struct onic_private *priv, u16 qid,
                                 u16 tail, u8 irq_arm);

int onic_vf_rings_init(struct onic_private *priv);
void onic_vf_rings_clear(struct onic_private *priv);

#endif