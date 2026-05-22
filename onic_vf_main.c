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

static int onic_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent){
    return 0;
}

static void onic_vf_remove(struct pci_dev *pdev)
{
#ifdef CMS_SUPPORT
        static int xmc_remove=0;
#endif
    // struct onic_private *priv = pci_get_drvdata(pdev);
    // unregister_netdev(priv->netdev);
    // onic_clear_interrupt(priv);
    // onic_clear_hardware(priv);
    // onic_clear_capacity(priv);
    // free_netdev(priv->netdev);
    // pci_set_drvdata(pdev, NULL);
    // pci_release_mem_regions(pdev);
    // pci_disable_device(pdev);

#ifdef CMS_SUPPORT
        /* Support XMC sensors (lm-sensors) */
        if(xmc_remove == 0)
        {
             xocl_fini_xmc();
            xmc_remove=1;
        }
#endif
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