#ifndef ONIC_VF_HW_H
#define ONIC_VF_HW_H

#include <linux/types.h>
#include "onic.h"

struct onic_vf_hardware {
    void __iomem *bar0;
    void __iomem *bar2;
    resource_size_t bar0_len;
    resource_size_t bar2_len;
};


int onic_vf_map_bars(struct onic_private *priv);
void onic_vf_unmap_bars(struct onic_private *priv);

#endif /* ONIC_VF_HW_H */