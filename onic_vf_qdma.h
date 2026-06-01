#ifndef __ONIC_VF_QDMA_H__
#define __ONIC_VF_QDMA_H__

struct onic_private;

int onic_vf_qdma_init(struct onic_private *priv);
void onic_vf_qdma_clear(struct onic_private *priv);

#endif