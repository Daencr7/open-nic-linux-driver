#ifndef ONIC_SRIOV_H
#define ONIC_SRIOV_H

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "qdma_context.h"
#include "qdma_device.h"
#include "onic.h"



/**
 * onic_sriov_configure - configure SR-IOV for the device
 * @pdev: pointer to PCI device
 * @num_vfs: number of VFs to configure
 */
int onic_sriov_configure(struct pci_dev *pdev, int num_vfs);

/**
 * onic_config_vf_resources - Cấu hình QDMA FMAP (Base Queue & Max Queues) cho VFs
 * @priv: Con trỏ tới onic_private của PF
 * @num_vfs: Số lượng VF cần kích hoạt
 */
int onic_config_vf_resources(struct onic_private *priv, int num_vfs);

/**
 * onic_free_vf_resources - Giải phóng tài nguyên cho VFs
 * @priv: Con trỏ tới onic_private của PF
 * @num_vfs: Số lượng VF cần giải phóng tài nguyên
 */
void onic_free_vf_resources(struct onic_private *priv, int num_vfs);

#endif /* ONIC_SRIOV_H */