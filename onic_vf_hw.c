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
