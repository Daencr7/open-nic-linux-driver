#ifndef _ONIC_VF_MBOX_H_
#define _ONIC_VF_MBOX_H_
#include <linux/types.h>

struct onic_private;

int onic_vf_mbox_process_one(struct onic_private *priv);
int onic_vf_mbox_get_queue_resource(struct onic_private *priv);
int onic_vf_mbox_irq_init(struct onic_private *priv, u16 vector);
void onic_vf_mbox_irq_clear(struct onic_private *priv);
#endif /* _ONIC_VF_MBOX_H_ */