#ifndef ONIC_VF_HW_H
#define ONIC_VF_HW_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "onic.h"
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "onic_mbox.h"

struct onic_private;


struct onic_vf_hardware {
    struct qdma_dev *qdev;
    u32 mbox_seq;
    struct completion mbox_done;
    struct mutex mbox_lock;
    struct onic_mbox_msg mbox_resp;
    bool mbox_irq_allocated;
    u16 mbox_vector;

    u16 func_id;
    u16 qbase;
    u16 qmax;

    u16 num_tx_queues;
    u16 num_rx_queues;

    u8 mac[ETH_ALEN];

    bool resource_valid;

    void __iomem *bar0;
    void __iomem *bar2;
    resource_size_t bar0_len;
    resource_size_t bar2_len;



};


int onic_vf_map_bars(struct onic_private *priv);

void onic_vf_unmap_bars(struct onic_private *priv);


u32 onic_vf_read_bar0(struct onic_private *priv, u32 offset);
void onic_vf_write_bar0(struct onic_private *priv, u32 offset, u32 val);

u32 onic_vf_read_bar2(struct onic_private *priv, u32 offset);
void onic_vf_write_bar2(struct onic_private *priv, u32 offset, u32 val);

#endif /* ONIC_VF_HW_H */