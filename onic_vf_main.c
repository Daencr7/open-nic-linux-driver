/* - khai báo VF PCI ID table
*- đăng ký pci_driver cho VF
*- onic_vf_probe()
*- onic_vf_remove()
*- module_init/module_exit
*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>
#include <linux/bpf.h>
#include "onic.h"
#include "onic_vf_netdev.h"
#include "onic_vf_hw.h"
#include "onic_vf_mbox.h"
#include "onic_vf_qdma.h"
#include "qdma_register.h"

#define DRV_STR "OpenNIC Linux Kernel Driver (VF)"
char onic_drv_name[] = "onic_vf";
#define DRV_VER "0.21"
const char onic_drv_str[] = DRV_STR;
const char onic_drv_ver[] = DRV_VER;

#define ONIC_VF_NON_Q_VECTORS 1

// static irqreturn_t onic_vf_debug_irq_handler(int irq, void *data)
// {
// 	struct onic_private *priv = data;
// 	int i;

// 	for (i = 1; i < 8; i++) {
// 		if (pci_irq_vector(priv->pdev, i) == irq)
// 			break;
// 	}

// 	dev_err(&priv->pdev->dev,
// 		"VF DEBUG IRQ hit: vector_index=%d linux_irq=%d mbox_sts=0x%08x vec=0x%08x ctrl=0x%08x\n",
// 		i, irq,
// 		onic_vf_read_bar0(priv, QDMA_VF_MBOX_STS),
// 		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_VEC),
// 		onic_vf_read_bar0(priv, QDMA_VF_MBOX_INTR_CTRL));

// 	return IRQ_HANDLED;
// }

MODULE_AUTHOR("Edna");
MODULE_DESCRIPTION(DRV_STR);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VER);


static const struct pci_device_id onic_vf_pci_tbl[] = {
    /** Gen 3 VF **/
    /** PCIe lane width x1 **/
    { PCI_DEVICE(0x10ee, 0xa031), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa131), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa231), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa331), }, /* VF on PF 3 */
    /** PCIe lane width x4 **/
    { PCI_DEVICE(0x10ee, 0xa034), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa134), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa234), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa334), }, /* VF on PF 3 */
    /** PCIe lane width x8 **/
    { PCI_DEVICE(0x10ee, 0xa038), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa138), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa238), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa338), }, /* VF on PF 3 */
    /** PCIe lane width x16 **/
    { PCI_DEVICE(0x10ee, 0xa03f), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa13f), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa23f), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa33f), }, /* VF on PF 3 */

	/** Gen 4 VF */
	/** PCIe lane width x1 */
    { PCI_DEVICE(0x10ee, 0xa041), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa141), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa241), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa341), }, /* VF on PF 3 */
    /** PCIe lane width x4 */
    { PCI_DEVICE(0x10ee, 0xa044), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa144), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa244), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa344), }, /* VF on PF 3 */
    /** PCIe lane width x8 */
    { PCI_DEVICE(0x10ee, 0xa048), }, /* VF on PF 0 */
    { PCI_DEVICE(0x10ee, 0xa148), }, /* VF on PF 1 */
    { PCI_DEVICE(0x10ee, 0xa248), }, /* VF on PF 2 */
    { PCI_DEVICE(0x10ee, 0xa348), }, /* VF on PF 3 */

    { 0,}
};

MODULE_DEVICE_TABLE(pci, onic_vf_pci_tbl);

/**
 * Default MAC address for 4 VFs
 * This task will dev in the future when max VFS > 4
 */
static const u8 onic_vf_default_macs[4][ETH_ALEN] = {
	{ 0x02, 0x0A, 0x35, 0x00, 0x00, 0x04 },
	{ 0x02, 0x0A, 0x35, 0x00, 0x00, 0x05 },
	{ 0x02, 0x0A, 0x35, 0x00, 0x00, 0x06 },
	{ 0x02, 0x0A, 0x35, 0x00, 0x00, 0x07 },
};

static int onic_vf_set_default_mac(struct onic_private *priv)
{
	struct net_device *netdev = priv->netdev;
	u16 func_id = priv->vf_hw.func_id;
	u16 vf_idx;

	if (func_id < 4 || func_id >= 8)
		return -EINVAL;

	vf_idx = func_id - 4;

	eth_hw_addr_set(netdev, onic_vf_default_macs[vf_idx]);

	dev_info(&priv->pdev->dev,
		 "VF default MAC assigned: func_id=%u vf_idx=%u mac=%pM\n",
		 func_id, vf_idx, netdev->dev_addr);

	return 0;
}

static const struct net_device_ops onic_vf_netdev_ops = {
	.ndo_open = onic_vf_open_netdev,
	.ndo_stop = onic_vf_stop_netdev,
	.ndo_start_xmit = onic_vf_xmit_frame,
	// .ndo_set_mac_address = onic_set_mac_address,
	// .ndo_do_ioctl = onic_do_ioctl,
	// .ndo_change_mtu = onic_change_mtu,
	// .ndo_get_stats64 = onic_get_stats64,
	// .ndo_bpf = onic_xdp,
};



static int onic_vf_probe(struct pci_dev *pdev,
			 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct onic_private *priv;
	int err;
	int vectors;
	// u32 build_ts;
	// int debug_irq_count = 0;

	dev_info(&pdev->dev, "OpenNIC VF probe start\n");

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device_mem failed: %d\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "DMA mask setup failed: %d\n", err);
			goto err_disable_device;
		}
	}

	err = pci_request_mem_regions(pdev, onic_drv_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_mem_regions failed: %d\n", err);
		goto err_disable_device;
	}

	pci_set_master(pdev);

	// alloc queue following queue per vf - qmax
	netdev = alloc_etherdev_mq(sizeof(struct onic_private), ONIC_VF_MAX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	priv = netdev_priv(netdev);
	memset(priv, 0, sizeof(*priv));

	priv->pdev = pdev;
	priv->netdev = netdev;

	pci_set_drvdata(pdev, priv);
	// pci_set_drvdata(pdev, pdev); /* VF chưa có private data vì chưa init datapath thật */
	/*	
	 MAP VF BAR0/BAR2
	 BAR0: QDMA/VF register /MSI-X/mailbox region
	 BAR2: shell register region (if have)
	*/
	/* map BARs */
	err = onic_vf_map_bars(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to map VF BARs: %d\n", err);
		goto err_free_netdev;
	}
	/* allocate MSI-X vectors
	 * - 1 for mailbox interrupt
	 * - rest for queue interrupts (if have)
	 */
	// vectors = pci_alloc_irq_vectors(pdev,
	// 			ONIC_VF_NON_Q_VECTORS,
	// 			ONIC_MAX_QUEUES + ONIC_VF_NON_Q_VECTORS,
	// 			PCI_IRQ_MSIX);
	vectors = pci_alloc_irq_vectors(pdev,
				ONIC_VF_NON_Q_VECTORS,
				ONIC_VF_NON_Q_VECTORS + 7,
				PCI_IRQ_MSIX);
	// vectors = pci_alloc_irq_vectors(pdev,
	// 			8,
	// 			8,
	// 			PCI_IRQ_MSIX);
	if (vectors < 0) {
		dev_err(&pdev->dev,
			"Failed to allocate VF MSI-X vectors: %d\n", vectors);
		err = vectors;
		goto err_unmap_bars;
	}
	dev_info(&pdev->dev,
		"VF MSI-X allocated: total=%d non_q=%u q_vectors=%u\n",
		vectors, ONIC_VF_NON_Q_VECTORS,
		vectors - ONIC_VF_NON_Q_VECTORS);

	priv->num_q_vectors = vectors - ONIC_VF_NON_Q_VECTORS;
	// priv->num_q_vectors = 0; // Tạm thời chưa triển khai queue interrupt cho VF, sẽ thêm sau khi init datapath thật
	
	// err = onic_vf_mbox_irq_init(priv, priv->num_q_vectors);
	err = onic_vf_mbox_irq_init(priv, 0);
	if (err)
		goto err_free_irq_vectors;

	// {
	// 	int i;

	// 	for (i = 1; i < vectors; i++) {
	// 		err = request_irq(pci_irq_vector(pdev, i),
	// 				onic_vf_debug_irq_handler,
	// 				0, "onic-vf-debug", priv);
	// 		if (err) {
	// 			dev_err(&pdev->dev,
	// 				"Failed to request VF debug IRQ vector=%d irq=%d err=%d\n",
	// 				i, pci_irq_vector(pdev, i), err);
	// 			goto err_clear_debug_irqs;
	// 		}

	// 		dev_info(&pdev->dev,
	// 			"VF debug IRQ requested: vector_index=%d linux_irq=%d\n",
	// 			i, pci_irq_vector(pdev, i));
	// 		debug_irq_count++;
	// 	}
	// }

	//VF request mailbox resource from PF 
	err = onic_vf_mbox_get_queue_resource(priv);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to get VF queue resource: %d\n", err);
		goto err_clear_mbox_irq;
	}
	dev_info(&pdev->dev,
	 "VF mailbox resource granted: function_id=%u qbase=%u qmax=%u global_queues=%u..%u\n",
	 priv->vf_hw.func_id,
	 priv->vf_hw.qbase,
	 priv->vf_hw.qmax,
	 priv->vf_hw.qbase,
	 priv->vf_hw.qbase + priv->vf_hw.qmax - 1);
	 
	err = onic_vf_qdma_init(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize VF QDMA state: %d\n", err);
		goto err_clear_vf_qdma;
	}

	err = netif_set_real_num_tx_queues(netdev, priv->vf_hw.num_tx_queues);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to set VF real TX queues=%u err=%d\n",
			priv->vf_hw.num_tx_queues, err);
		goto err_clear_vf_qdma;
	}

	err = netif_set_real_num_rx_queues(netdev, priv->vf_hw.num_rx_queues);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to set VF real RX queues=%u err=%d\n",
			priv->vf_hw.num_rx_queues, err);
		goto err_clear_vf_qdma;
	}
	dev_info(&pdev->dev,
	 "VF netdev queues: tx=%u rx=%u q_vectors=%u\n",
	 priv->vf_hw.num_tx_queues,
	 priv->vf_hw.num_rx_queues,
	 priv->num_q_vectors);
	/*
	 * Tạm thời VF chưa init datapath thật.
	 * Sau này sẽ thêm:
	 * - init mailbox
	 * - request qbase/qmax từ PF
	 * - init TX/RX queue
	 */

	netdev->netdev_ops = &onic_vf_netdev_ops;

	/* Create a ramdom MAC address for VF netdev
	 * Need to fix when using mailbox to get MAC from PF if want change
	 */
	// eth_hw_addr_random(netdev); 
	err = onic_vf_set_default_mac(priv);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to assign default VF MAC: func_id=%u err=%d\n",
			priv->vf_hw.func_id, err);
		goto err_clear_vf_qdma;
	}


	/* set MTU */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = 9000;

	// Have not implemented XDP for VF yet, so disable it for now
	netif_carrier_off(netdev);



	

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "register_netdev failed: %d\n", err);
		goto err_clear_vf_qdma;
	}

	dev_info(&pdev->dev, "OpenNIC VF probe success, netdev=%s\n",
		 netdev->name);

	return 0;

err_clear_vf_qdma:
	onic_vf_qdma_clear(priv);

// err_clear_debug_irqs:
// {
// 	int i;

// 	for (i = 1; i <= debug_irq_count; i++)
// 		free_irq(pci_irq_vector(pdev, i), priv);
// }

err_clear_mbox_irq:
	onic_vf_mbox_irq_clear(priv);

err_free_irq_vectors:
	priv->num_q_vectors = 0;
	pci_free_irq_vectors(pdev);

err_unmap_bars:
	onic_vf_unmap_bars(priv);

err_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);

err_release_regions:
	pci_release_mem_regions(pdev);

err_disable_device:
	pci_disable_device(pdev);

	return err;
}

static void onic_vf_remove(struct pci_dev *pdev)
{
	struct onic_private *priv = pci_get_drvdata(pdev);
	struct net_device *netdev = NULL;

	dev_info(&pdev->dev, "OpenNIC VF remove\n");

	if (priv)
		netdev = priv->netdev;

	if (netdev)
		unregister_netdev(netdev);

	if (priv) {
		onic_vf_qdma_clear(priv);
		onic_vf_mbox_irq_clear(priv);
		priv->num_q_vectors = 0;
		// {
		// 	int i;

		// 	for (i = 1; i < 8; i++)
		// 		free_irq(pci_irq_vector(pdev, i), priv);
		// }
		pci_free_irq_vectors(pdev);
		onic_vf_unmap_bars(priv);
	}

	pci_set_drvdata(pdev, NULL);

	if (netdev)
		free_netdev(netdev);

	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

// static void onic_vf_remove(struct pci_dev *pdev)
// {
// 	struct onic_private *priv = pci_get_drvdata(pdev);
// 	struct net_device *netdev = NULL;

// 	dev_info(&pdev->dev, "OpenNIC VF remove\n");

// 	if (priv)
// 		netdev = priv->netdev;

// 	pci_set_drvdata(pdev, NULL);

// 	if (netdev) {
// 		unregister_netdev(netdev);
// 		free_netdev(netdev);
// 	}

// 	pci_release_mem_regions(pdev);
// 	pci_disable_device(pdev);
// }

static struct pci_driver onic_vf_pci_driver = {
    .name = onic_drv_name,
    .id_table = onic_vf_pci_tbl,
    .probe = onic_vf_probe,
    .remove = onic_vf_remove,
};

static int __init onic_init_module(void)
{
	// pr_info("%s %s", onic_drv_str, onic_drv_ver);
	return pci_register_driver(&onic_vf_pci_driver);
}

static void __exit onic_exit_module(void)
{
	pci_unregister_driver(&onic_vf_pci_driver);
}

module_init(onic_init_module);
module_exit(onic_exit_module);