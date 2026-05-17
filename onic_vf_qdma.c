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
 */