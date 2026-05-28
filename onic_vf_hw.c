/* Hardware-specific local functions for VF */
/*
*- map VF BAR
*- unmap VF BAR
*- read/write VF local register
*- init VF-local hardware nếu có
*
*/
/*
- không init CMAC
- không init QDMA global CSR
- không enable SR-IOV
- không quản lý VF khác
- không reset global hardware

VF cần truy cập BAR của chính nó.
Nhưng VF chỉ nên dùng phần register cho:
- mailbox inbox/outbox
- queue doorbell / PIDX update
- interrupt status nếu có
VF không nên ghi:
- global QDMA CSR
- context command global nếu không được phép
- CMAC control
- shell reset
*/

#include <linux/pci.h>
#include "onic.h"
#include "onic_vf_hw.h"


#define ONIC_VF_QDMA_BAR   0
#define ONIC_VF_SHELL_BAR  2

int onic_vf_map_bars(struct onic_private *priv)
{
	struct pci_dev *pdev = priv->pdev;

	priv->vf_hw.bar0_len = pci_resource_len(pdev, ONIC_VF_QDMA_BAR);
	priv->vf_hw.bar2_len = pci_resource_len(pdev, ONIC_VF_SHELL_BAR);

	dev_info(&pdev->dev, "VF BAR0 len=%pa\n", &priv->vf_hw.bar0_len);
	dev_info(&pdev->dev, "VF BAR2 len=%pa\n", &priv->vf_hw.bar2_len);

	if (!priv->vf_hw.bar0_len) {
		dev_err(&pdev->dev, "VF BAR0 not available\n");
		return -ENODEV;
	}

	priv->vf_hw.bar0 = pci_iomap(pdev, ONIC_VF_QDMA_BAR, 0);
	
	if (!priv->vf_hw.bar0) {
		dev_err(&pdev->dev, "failed to map VF BAR0\n");
		return -ENOMEM;
	}

	if (priv->vf_hw.bar2_len) {
		priv->vf_hw.bar2 = pci_iomap(pdev, ONIC_VF_SHELL_BAR, 0);
		if (!priv->vf_hw.bar2) {
			dev_err(&pdev->dev, "failed to map VF BAR2\n");
			pci_iounmap(pdev, priv->vf_hw.bar0);
			priv->vf_hw.bar0 = NULL;
			return -ENOMEM;
		}
	}
	
	// priv->vf_hw.qdma.pdev = pdev;
	// priv->vf_hw.qdma.func_id = PCI_FUNC(pdev->devfn);
	// if (priv->vf_hw.bar0)
	// 	priv->vf_hw.qdma_hw.addr = priv->vf_hw.bar0;

	// if (priv->vf_hw.bar2)
	// 	priv->vf_hw.shell_hw.addr = priv->vf_hw.bar2;

	dev_info(&pdev->dev, "VF BAR0 mapped addr=%p len=%pa\n",
		 priv->vf_hw.bar0, &priv->vf_hw.bar0_len);
	dev_info(&pdev->dev, "VF BAR2 mapped addr=%p len=%pa\n",
		 priv->vf_hw.bar2, &priv->vf_hw.bar2_len);

	return 0;
}

void onic_vf_unmap_bars(struct onic_private *priv)
{
	struct pci_dev *pdev = priv->pdev;

	if (priv->vf_hw.bar2) {
		pci_iounmap(pdev, priv->vf_hw.bar2);
		priv->vf_hw.bar2 = NULL;
		priv->vf_hw.bar2_len = 0;
	}

	if (priv->vf_hw.bar0) {
		pci_iounmap(pdev, priv->vf_hw.bar0);
		priv->vf_hw.bar0 = NULL;
		priv->vf_hw.bar0_len = 0;
	}
	// priv->vf_hw.qdma.addr = NULL;
	// priv->vf_hw.shell.addr = NULL;	
}


#include "onic.h"
#include "onic_vf_hw.h"

static bool onic_vf_bar_range_ok(resource_size_t len, u32 offset)
{
	if (!len)
		return false;

	if (offset > len - sizeof(u32))
		return false;

	return true;
}

u32 onic_vf_read_bar0(struct onic_private *priv, u32 offset)
{
	if (!priv || !priv->vf_hw.bar0)
		return 0xffffffff;

	if (!onic_vf_bar_range_ok(priv->vf_hw.bar0_len, offset))
		return 0xffffffff;

	return ioread32(priv->vf_hw.bar0 + offset);
}

void onic_vf_write_bar0(struct onic_private *priv, u32 offset, u32 val)
{
	if (!priv || !priv->vf_hw.bar0)
		return;

	if (!onic_vf_bar_range_ok(priv->vf_hw.bar0_len, offset))
		return;

	iowrite32(val, priv->vf_hw.bar0 + offset);
}

u32 onic_vf_read_bar2(struct onic_private *priv, u32 offset)
{
	if (!priv || !priv->vf_hw.bar2)
		return 0xffffffff;

	if (!onic_vf_bar_range_ok(priv->vf_hw.bar2_len, offset))
		return 0xffffffff;

	return ioread32(priv->vf_hw.bar2 + offset);
}

void onic_vf_write_bar2(struct onic_private *priv, u32 offset, u32 val)
{
	if (!priv || !priv->vf_hw.bar2)
		return;

	if (!onic_vf_bar_range_ok(priv->vf_hw.bar2_len, offset))
		return;

	iowrite32(val, priv->vf_hw.bar2 + offset);
}