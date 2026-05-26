/** 
 * This netdev ops just for VF
 * - open VF netdev
 * - stop VF netdev
 * - transmit packet
 * - change MAC
 * - change MTU
 * - get stats
 * 
 * 
 * Đây là phần Linux network interface của VF.
 * 
- ndo_open
- ndo_stop
- ndo_start_xmit
- ndo_get_stats64
- NAPI poll
- RX refill
- TX clean
Nhưng giai đoạn đầu có thể để tối giản:
open()  → return 0
stop()  → return 0
xmit()  → dev_kfree_skb(); return NETDEV_TX_OK
Mục tiêu đầu tiên chỉ là:
ip link thấy VF netdev
ip link set dev <vf> up không crash
Sau đó mới nối datapath thật.

 */