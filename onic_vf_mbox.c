/* Mailbox functions for VF */
/**
 * - init mailbox channel
 * - gửi request tới PF
 * - nhận reply từ PF
 * - lấy config ban đầu
 * - request set MAC/MTU/link/stats
 * - report reset/error
 * 
Đây là client phía VF.
VF cần các API kiểu:
int onic_vf_mbox_init(struct onic_private *priv);
int onic_vf_mbox_send(struct onic_private *priv, struct onic_mbox_msg *msg);
int onic_vf_mbox_recv(struct onic_private *priv, struct onic_mbox_msg *msg);
int onic_vf_mbox_get_resource(struct onic_private *priv);

VF probe
  ↓
onic_vf_mbox_init()
  ↓
VF gửi GET_RESOURCE
  ↓
PF xử lý
  ↓
PF trả qbase/qmax/MAC
  ↓
VF lưu vào priv


QDMA hỗ trợ mailbox giữa PF/VF; 
mỗi function có inbox 128B và outbox 128B, thường visible trong DMA BAR, 
và mỗi function có thể có một outgoing và một incoming mailbox message outstanding
 */

//  static void onic_mbox_write_msg(void __iomem *base, u32 off,
// 				const void *msg, size_t len)
// {
// 	size_t i;
// 	u32 word;

// 	for (i = 0; i < len; i += 4) {
// 		word = 0;
// 		memcpy(&word, (const u8 *)msg + i,
// 		       min_t(size_t, 4, len - i));
// 		iowrite32(word, base + off + i);
// 	}
// }

// static void onic_mbox_read_msg(void __iomem *base, u32 off,
// 			       void *msg, size_t len)
// {
// 	size_t i;
// 	u32 word;

// 	for (i = 0; i < len; i += 4) {
// 		word = ioread32(base + off + i);
// 		memcpy((u8 *)msg + i, &word,
// 		       min_t(size_t, 4, len - i));
// 	}
// }