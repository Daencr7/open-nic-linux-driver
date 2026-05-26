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

#define DRV_STR "OpenNIC Linux Kernel Driver (VF)"
char onic_drv_name[] = "onic_vf";
#define DRV_VER "0.21"
const char onic_drv_str[] = DRV_STR;
const char onic_drv_ver[] = DRV_VER;

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

static int onic_vf_probe(struct pci_dev *pdev,
			 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	// struct onic_private *priv;
	int err;

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

	// netdev = alloc_etherdev_mq(sizeof(struct onic_private), 1);
	// if (!netdev) {
	// 	err = -ENOMEM;
	// 	goto err_release_regions;
	// }

	// SET_NETDEV_DEV(netdev, &pdev->dev);

	// priv = netdev_priv(netdev);
	// memset(priv, 0, sizeof(*priv));

	// priv->pdev = pdev;
	// priv->netdev = netdev;

	// pci_set_drvdata(pdev, priv);
	pci_set_drvdata(pdev, pdev); /* VF chưa có private data vì chưa init datapath thật */
	/*
	 * Tạm thời VF chưa init datapath thật.
	 * Sau này sẽ thêm:
	 * - map BAR
	 * - init mailbox
	 * - request qbase/qmax từ PF
	 * - init TX/RX queue
	 */

	// eth_hw_addr_random(netdev);

	/*
	 * Nếu bạn đã có VF netdev_ops thì mở dòng này:
	 *
	 * netdev->netdev_ops = &onic_vf_netdev_ops;
	 */

	// err = register_netdev(netdev);
	// if (err) {
	// 	dev_err(&pdev->dev, "register_netdev failed: %d\n", err);
	// 	goto err_free_netdev;
	// }

	dev_info(&pdev->dev, "OpenNIC VF probe success, netdev=%s\n",
		 netdev->name);

	return 0;

// err_free_netdev:
// 	pci_set_drvdata(pdev, NULL);
// 	free_netdev(netdev);

// err_release_regions:
// 	pci_release_mem_regions(pdev);

err_disable_device:
	pci_disable_device(pdev);

	return err;
}


static void onic_vf_remove(struct pci_dev *pdev)
{
	struct onic_private *priv = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "OpenNIC VF remove\n");

	if (priv) {
		if (priv->netdev)
			unregister_netdev(priv->netdev);

		pci_set_drvdata(pdev, NULL);

		if (priv->netdev)
			free_netdev(priv->netdev);
	}

	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

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