#ifndef __ONIC_MBOX_H__
#define __ONIC_MBOX_H__
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include "onic.h"
#include "onic_vf.h"
#include "mbox.h"
#include "qdma_device.h"
#include "qdma_register.h"

int onic_pf_mbox_poll(struct onic_private *priv);

void onic_pf_mbox_start(struct onic_private *priv);
void onic_pf_mbox_stop(struct onic_private *priv);

#endif /* __ONIC_MBOX_H__ */