/**
 * Queue/QDMA just for VF, not shared with PF
 * - init TX queue của VF
 * - init RX queue của VF
 * - clear TX/RX queue
 * - setup descriptor ring
 * - setup completion ring
 * - ring doorbell
 */

/* VF không tự chọn queue.
VF dùng qbase/qmax do PF trả qua mailbox.

Sau khi VF nhận resource từ PF:
qbase = ...
qmax  = ...
VF mới được init queue.

Ở đây phải phân biệt:
local queue index của VF: 0, 1, 2, ...
global QDMA queue ID: qbase + local_index
Ví dụ PF cấp:
VF0 qbase=4 qmax=4
local queue 0 → global queue 4
local queue 1 → global queue 5
local queue 2 → global queue 6
local queue 3 → global queue 7
Trong code VF nên có helper:
static inline u16 onic_vf_global_qid(struct onic_private *priv, u16 local_qid)
{
	return priv->qbase + local_qid;
}
 */