/* SR-IOV functions for ONIC */
/**
 * - xử lý sriov_configure
 * - enable VF
 * - disable VF
 * - cấp resource cho VF
 * - lưu bảng vf_info
 */

#include "onic_sriov.h"



int onic_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct onic_private *priv = pci_get_drvdata(pdev);
	int rv;
	if(num_vfs == 0) {
		if(pci_vfs_assigned(pdev)) {
			dev_err(&pdev->dev, "Cannot disable SR-IOV while VFs are assigned");
			return -EBUSY;
		}
		
		pci_disable_sriov(pdev);
		onic_free_vf_resources(priv, pci_num_vf(pdev));
		dev_info(&pdev->dev, "SR-IOV disabled");
		return 0;
	}


	if (!test_bit(ONIC_FLAG_MASTER_PF, priv->flags))
		return -EINVAL;

	if (num_vfs < 0 || num_vfs > ONIC_MAX_VFS)
		return -EINVAL;

	rv = onic_config_vf_resources(priv, num_vfs);
	if (rv < 0) {
		dev_err(&pdev->dev, "onic_config_vf_resources failed, err = %d", rv);
		return rv;
	}
	
	rv = pci_enable_sriov(pdev, num_vfs);

	if (rv < 0) {
		dev_err(&pdev->dev, "pci_enable_sriov failed, err = %d", rv);
		onic_free_vf_resources(priv, num_vfs);
		return rv;
	}
	dev_info(&pdev->dev, "SR-IOV enabled with %d VFs", num_vfs);

	return 0;
}


int onic_config_vf_resources(struct onic_private *priv, int num_vfs)
{
	struct qdma_dev *pf_qdev = (struct qdma_dev *)priv->hw.qdma;
	int i, err;
	int queues_per_vf = 4;
	int current_base_queue;

	if (!pf_qdev)
		return -ENODEV;

	current_base_queue = pf_qdev->q_base + pf_qdev->num_queues;

	for (i = 0; i < num_vfs; i++) {
		struct qdma_fmap_ctxt fmap_ctxt = {0};
		struct qdma_dev vf_qdev = {0};
		u16 vf_func_id = 4 + i;

		fmap_ctxt.qbase = current_base_queue;
		fmap_ctxt.qmax  = queues_per_vf;

		vf_qdev.pdev = priv->pdev;
		vf_qdev.addr = pf_qdev->addr;
		vf_qdev.func_id = vf_func_id;

		err = qdma_write_fmap_ctxt(&vf_qdev, &fmap_ctxt);
		if (err)
			return err;

		dev_info(&priv->pdev->dev,
			 "VF%d func_id=%u qbase=%d qmax=%d\n",
			 i, vf_func_id, current_base_queue, queues_per_vf);

		current_base_queue += queues_per_vf;
		priv->vf_res[i].vf_id = i;
		priv->vf_res[i].func_id = vf_func_id;
		priv->vf_res[i].qbase = current_base_queue;
		priv->vf_res[i].qmax = queues_per_vf;
		priv->vf_res[i].num_tx_queues = queues_per_vf;
		priv->vf_res[i].num_rx_queues = queues_per_vf;
		eth_random_addr(priv->vf_res[i].mac);
		priv->vf_res[i].enabled = true;
	}

    return 0;
}


void onic_free_vf_resources(struct onic_private *priv, int num_vfs)
{
	/* No resources to free in this implementation since we didn't allocate any per-VF resources */
}