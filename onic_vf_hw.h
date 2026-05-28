#ifndef ONIC_VF_HW_H
#define ONIC_VF_HW_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "onic.h"

struct onic_private;

struct onic_vf_hardware {
    void __iomem *bar0;
    void __iomem *bar2;
    resource_size_t bar0_len;
    resource_size_t bar2_len;

    // struct onic_hardware qdma_hw;   /* use for BAR0 */
	// struct onic_hardware shell_hw;  /* use for BAR2 */
};


int onic_vf_map_bars(struct onic_private *priv);

void onic_vf_unmap_bars(struct onic_private *priv);


u32 onic_vf_read_bar0(struct onic_private *priv, u32 offset);
void onic_vf_write_bar0(struct onic_private *priv, u32 offset, u32 val);

u32 onic_vf_read_bar2(struct onic_private *priv, u32 offset);
void onic_vf_write_bar2(struct onic_private *priv, u32 offset, u32 val);

#endif /* ONIC_VF_HW_H */