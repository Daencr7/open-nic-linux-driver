/* Mailbox header common for VF and PF 
VF muốn dùng hardware
nhưng VF không được tự cấu hình global hardware
nên VF phải hỏi PF
PF kiểm tra và cấp quyền
VF chỉ dùng phần được cấp

VF probe()
  ↓
VF enable PCI
  ↓
VF map BAR mailbox
  ↓
VF gửi message GET_RESOURCE
  ↓
PF nhận interrupt/mailbox event
  ↓
PF đọc request
  ↓
PF tra vf_res[vf_id]
  ↓
PF trả response
  ↓
VF nhận qbase/qmax/MAC
  ↓
VF init queue/netdev

enum onic_mbox_opcode {
	ONIC_MBOX_OP_GET_RESOURCE = 1,
	ONIC_MBOX_OP_GET_MAC,
	ONIC_MBOX_OP_SET_MAC,
	ONIC_MBOX_OP_GET_LINK,
	ONIC_MBOX_OP_RESET,
};

struct onic_mbox_hdr {
	u16 opcode;
	u16 vf_id;
	u16 seq;
	u16 status;
	u16 len;
};

struct onic_mbox_resource_resp {
	u16 qbase;
	u16 qmax;
	u16 num_tx_queues;
	u16 num_rx_queues;
	u8 mac[ETH_ALEN];
};
Request:
VF → PF:
opcode = GET_RESOURCE
vf_id  = VF number
Response:
PF → VF:
status = OK
qbase = 4
qmax = 4
mac = xx:xx:xx:xx:xx:xx
PF xử lý mailbox

PF phải có logic kiểu:
on mailbox interrupt:
  identify vf_id
  read inbox
  switch opcode
    GET_RESOURCE:
      copy vf_res[vf_id] vào response
    GET_MAC:
      trả MAC
    SET_MAC:
      kiểm tra policy rồi set
  write outbox
  notify VF

VF xử lý mailbox
VF thì đơn giản hơn:
send request
wait response
parse response
save resource
*/