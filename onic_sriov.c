/* SR-IOV functions for ONIC */
/**
 * - xử lý sriov_configure
 * - enable VF
 * - disable VF
 * - cấp resource cho VF
 * - lưu bảng vf_info
 */

#include "onic_sriov.h"
#include "onic_register.h"
#include "onic_common.h"


int onic_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct onic_private *priv = pci_get_drvdata(pdev);
	int enabled_vfs;
	int rv;
	if(num_vfs == 0) {
		if(pci_vfs_assigned(pdev)) {
			dev_err(&pdev->dev, "Cannot disable SR-IOV while VFs are assigned");
			return -EBUSY;
		}
		enabled_vfs = pci_num_vf(pdev);
		pci_disable_sriov(pdev);
		onic_free_vf_resources(priv, enabled_vfs);
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

	return num_vfs;
}


int onic_config_vf_resources(struct onic_private *priv, int num_vfs)
{
	struct qdma_dev *pf_qdev = (struct qdma_dev *)priv->hw.qdma;
	int i, err;
	// int queues_per_vf = 4;
	int current_base_queue;

	if (!pf_qdev)
		return -ENODEV;

	current_base_queue = pf_qdev->q_base + pf_qdev->num_queues;

	for (i = 0; i < num_vfs; i++) {
		struct qdma_fmap_ctxt fmap_ctxt = {0};
		struct qdma_dev vf_qdev = {0};
		u16 vf_func_id = 4 + i;
 
		fmap_ctxt.qbase = current_base_queue;
		fmap_ctxt.qmax  = ONIC_VF_MAX_QUEUES;

		vf_qdev.pdev = priv->pdev;
		vf_qdev.addr = pf_qdev->addr;
		vf_qdev.func_id = vf_func_id;

		err = qdma_write_fmap_ctxt(&vf_qdev, &fmap_ctxt);
		if (err) {
			qdma_clear_fmap_ctxt(&vf_qdev);
			onic_free_vf_resources(priv, i);
			return err;
		}

	{
		u32 val;
		int j;

		val = FIELD_SET(QDMA_FUNC_QCONF_QBASE_MASK, fmap_ctxt.qbase) |
			FIELD_SET(QDMA_FUNC_QCONF_NUMQ_MASK, fmap_ctxt.qmax);

		onic_write_reg(&priv->hw, QDMA_FUNC_OFFSET_QCONF(vf_func_id), val);

		for (j = 0; j < 128; j++) {
			val = (j % fmap_ctxt.qmax) & 0x0000ffff;
			onic_write_reg(&priv->hw,
					QDMA_FUNC_OFFSET_INDIR_TABLE(vf_func_id, j),
					val);
		}
	}

		priv->vf_res[i].vf_id = i;
		priv->vf_res[i].func_id = vf_func_id;
		priv->vf_res[i].qbase = current_base_queue;
		priv->vf_res[i].qmax = ONIC_VF_MAX_QUEUES;
		priv->vf_res[i].num_tx_queues = ONIC_VF_MAX_QUEUES;
		priv->vf_res[i].num_rx_queues = ONIC_VF_MAX_QUEUES;
		eth_random_addr(priv->vf_res[i].mac);
		priv->vf_res[i].enabled = true;
		dev_info(&priv->pdev->dev,
				"VF%d resource: func_id=%u qbase=%u qmax=%u txq=%u rxq=%u mac=%pM\n",
				i,
				priv->vf_res[i].func_id,
				priv->vf_res[i].qbase,
				priv->vf_res[i].qmax,
				priv->vf_res[i].num_tx_queues,
				priv->vf_res[i].num_rx_queues,
				priv->vf_res[i].mac);
		current_base_queue += ONIC_VF_MAX_QUEUES;
		
	}
	priv->num_vfs = num_vfs;

    return 0;
}


void onic_free_vf_resources(struct onic_private *priv, int num_vfs)
{
	struct qdma_dev *pf_qdev = (struct qdma_dev *)priv->hw.qdma;
	int i;
	int err;

	if (!pf_qdev)
		return;

	num_vfs = min_t(int, num_vfs, ONIC_MAX_VFS);

	for (i = 0; i < num_vfs; i++) {
		struct qdma_dev vf_qdev = {0};

		if (!priv->vf_res[i].enabled)
			continue;

		vf_qdev.pdev = priv->pdev;
		vf_qdev.addr = pf_qdev->addr;
		vf_qdev.func_id = priv->vf_res[i].func_id;


		{
			int j;
			onic_write_reg(&priv->hw,
					QDMA_FUNC_OFFSET_QCONF(priv->vf_res[i].func_id),
					0);
			for (j = 0; j < 128; j++)
				onic_write_reg(&priv->hw,
						QDMA_FUNC_OFFSET_INDIR_TABLE(priv->vf_res[i].func_id, j),
						0);
		}
		err = qdma_clear_fmap_ctxt(&vf_qdev);
		if (err)
			dev_warn(&priv->pdev->dev,
				 "Failed to clear VF%d FMAP, err=%d\n",
				 i, err);

		memset(&priv->vf_res[i], 0, sizeof(priv->vf_res[i]));
	}

	priv->num_vfs = 0;
}