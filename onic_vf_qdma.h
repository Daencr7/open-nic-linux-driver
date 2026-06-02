#ifndef __ONIC_VF_QDMA_H__
#define __ONIC_VF_QDMA_H__

struct onic_private;
struct onic_tx_queue;

int onic_vf_qdma_init(struct onic_private *priv);
void onic_vf_qdma_clear(struct onic_private *priv);
int onic_vf_q_irq_init(struct onic_private *priv);
void onic_vf_q_irq_clear(struct onic_private *priv);


void onic_vf_set_tx_head(struct onic_private *priv, u16 qid, u16 head);
void onic_vf_set_rx_head(struct onic_private *priv, u16 qid, u16 head);
void onic_vf_set_completion_tail(struct onic_private *priv, u16 qid, u16 tail, u8 irq_arm);

void onic_vf_tx_clean(struct onic_private *priv, struct onic_tx_queue *q);

int onic_vf_tx_contexts_init(struct onic_private *priv);
int onic_vf_tx_contexts_clear(struct onic_private *priv);
int onic_vf_rx_contexts_init(struct onic_private *priv);
int onic_vf_rx_contexts_clear(struct onic_private *priv);
int onic_vf_rx_datapath_init(struct onic_private *priv);
void onic_vf_rx_datapath_clear(struct onic_private *priv);

int onic_vf_rings_init(struct onic_private *priv);
void onic_vf_rings_clear(struct onic_private *priv);

#endif