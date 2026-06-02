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
#include <linux/percpu.h>

#include "onic.h"
#include "onic_vf_netdev.h"
#include "onic_vf_hw.h"
#include "onic_vf_mbox.h"
#include "onic_vf_qdma.h"


#define DRV_STR "OpenNIC Linux Kernel Driver (VF)"
char onic_drv_name[] = "onic_vf";
#define DRV_VER "0.21"
const char onic_drv_str[] = DRV_STR;
const char onic_drv_ver[] = DRV_VER;

#define ONIC_VF_NON_Q_VECTORS 1

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

static const struct net_device_ops onic_vf_netdev_ops = {
	.ndo_open = onic_vf_open_netdev,
	.ndo_stop = onic_vf_stop_netdev,
	.ndo_start_xmit = onic_vf_xmit_frame,
	// .ndo_set_mac_address = onic_set_mac_address,
	// .ndo_do_ioctl = onic_do_ioctl,
	// .ndo_change_mtu = onic_change_mtu,
	.ndo_get_stats64 = onic_vf_get_stats64,
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
	int cpu;

	dev_info(&pdev->dev, "OpenNIC VF probe start\n");

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device_mem failed: %d\n", err);
		return err;
	}

	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA mask setup failed: %d\n", err);
		goto err_disable_device;
	}

	/* QDMA descriptor rings require 32-bit coherent DMA addresses. */
	err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "Coherent DMA mask setup failed: %d\n", err);
		goto err_disable_device;
	}

	err = pci_request_mem_regions(pdev, onic_drv_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_mem_regions failed: %d\n", err);
		goto err_disable_device;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct onic_private),
                           		ONIC_MAX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	priv = netdev_priv(netdev);
	memset(priv, 0, sizeof(*priv));

	priv->pdev = pdev;
	priv->netdev = netdev;

	priv->netdev_stats = alloc_percpu(struct rtnl_link_stats64);
	if (!priv->netdev_stats) {
		err = -ENOMEM;
		goto err_free_netdev;
	}

	for_each_possible_cpu(cpu)
		memset(per_cpu_ptr(priv->netdev_stats, cpu), 0,
			sizeof(struct rtnl_link_stats64));

	pci_set_drvdata(pdev, priv);
	// pci_set_drvdata(pdev, pdev); /* VF chưa có private data vì chưa init datapath thật */
	/*	
	 MAP VF BAR0/BAR2
	 BAR0: QDMA/VF register /MSI-X/mailbox region
	 BAR2: shell register region (if have)
	*/
	err = onic_vf_map_bars(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to map VF BARs: %d\n", err);
		goto err_free_netdev;
	}
	vectors = pci_alloc_irq_vectors(pdev,
				ONIC_VF_NON_Q_VECTORS + 1,
				ONIC_MAX_QUEUES + ONIC_VF_NON_Q_VECTORS,
				PCI_IRQ_MSIX);
	if (vectors < 0) {
		dev_err(&pdev->dev,
			"Failed to allocate VF MSI-X vectors: %d\n", vectors);
		err = vectors;
		goto err_unmap_bars;
	}

	priv->num_q_vectors = vectors - ONIC_VF_NON_Q_VECTORS;

	err = onic_vf_mbox_irq_init(priv, priv->num_q_vectors);
	if (err)
		goto err_free_irq_vectors;

	err = onic_vf_mbox_get_queue_resource(priv);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to get VF queue resource: %d\n", err);
		goto err_clear_mbox_irq;
	}
	err = onic_vf_qdma_init(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize VF QDMA state: %d\n", err);
		goto err_clear_mbox_irq;
	}
	priv->num_q_vectors = min_t(u16, priv->num_q_vectors,
								priv->vf_hw.qmax);
	if (!priv->num_q_vectors) {
		err = -ENOSPC;
		goto err_clear_vf_qdma;
	}

	priv->num_tx_queues = priv->num_q_vectors;
	priv->num_rx_queues = priv->num_q_vectors;
	
	priv->vf_hw.num_tx_queues = priv->num_q_vectors;
	priv->vf_hw.num_rx_queues = priv->num_q_vectors;

	err = netif_set_real_num_tx_queues(netdev,
									priv->vf_hw.num_tx_queues);
	if (err)
		goto err_clear_vf_qdma;

	err = netif_set_real_num_rx_queues(netdev,
									priv->vf_hw.num_rx_queues);
	if (err)
		goto err_clear_vf_qdma;

	dev_info(&pdev->dev,
			"VF active queues: tx=%u rx=%u q_vectors=%u\n",
			priv->vf_hw.num_tx_queues,
			priv->vf_hw.num_rx_queues,
			priv->num_q_vectors);

	err = onic_vf_q_irq_init(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize VF queue IRQs: %d\n",
				err);
		goto err_clear_vf_qdma;
	}
	/*
	 * Tạm thời VF chưa init datapath thật.
	 * Sau này sẽ thêm:
	 * - init mailbox
	 * - request qbase/qmax từ PF
	 * - init TX/RX queue
	 */

	netdev->netdev_ops = &onic_vf_netdev_ops;
	// Create a ramdom MAC address for VF netdev
	// Need to fix when using mailbox to get MAC from PF
	eth_hw_addr_random(netdev); 


	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ETH_DATA_LEN;

	// Have not implemented XDP for VF yet, so disable it for now
	netif_carrier_off(netdev);



	

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "register_netdev failed: %d\n", err);
		goto err_clear_q_irqs;
	}

	dev_info(&pdev->dev, "OpenNIC VF probe success, netdev=%s\n",
		 netdev->name);

	return 0;
err_clear_q_irqs:
    onic_vf_q_irq_clear(priv);

err_clear_vf_qdma:
	onic_vf_qdma_clear(priv);

err_clear_mbox_irq:
	onic_vf_mbox_irq_clear(priv);

err_free_irq_vectors:
	priv->num_q_vectors = 0;
	pci_free_irq_vectors(pdev);

err_unmap_bars:
	onic_vf_unmap_bars(priv);

err_free_netdev:
	if (priv->netdev_stats) {
		free_percpu(priv->netdev_stats);
		priv->netdev_stats = NULL;
	}
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
		onic_vf_rx_datapath_clear(priv);
		
		if (onic_vf_rx_contexts_clear(priv))
            dev_warn(&pdev->dev,
                     "Failed to clear VF RX contexts during remove\n");

        if (onic_vf_tx_contexts_clear(priv))
            dev_warn(&pdev->dev,
                     "Failed to clear VF TX contexts during remove\n");

        onic_vf_rings_clear(priv);
        onic_vf_q_irq_clear(priv);
		onic_vf_qdma_clear(priv);
		onic_vf_mbox_irq_clear(priv);
		priv->num_q_vectors = 0;
		pci_free_irq_vectors(pdev);
		onic_vf_unmap_bars(priv);
		if (priv->netdev_stats) {
			free_percpu(priv->netdev_stats);
			priv->netdev_stats = NULL;
		}
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