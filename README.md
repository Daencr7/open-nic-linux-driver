# AMD OpenNIC Driver
The developed driver package includes two Linux kernel network drivers: a Physical Function (PF) driver and a Virtual Function (VF) driver for an FPGA-based SmartNIC. The drivers support PCIe device management, QDMA-based data movement, SR-IOV virtualization, and multi-queue network interfaces for 10 GbE operation.

## Building the Driver

Follow the steps below to build the driver.

1. Run `make` to compile the loadable kernel module `onic.ko`.
2. Connect 100Gbps cables/loopback adapters to enabled ports before insert the
   kernel module.  Currently, the driver does not detect link status change.
   Thus links should be ready before loading the driver.
3. Run `sudo insmod onic.ko` to insert the kernel module. (There is an optional 
   parameter RS_FEC_ENABLED, which can be set to either zero or one.)
5. Verify that no error message is printed through `dmesg`, and new devices show
   up in `ifconfig` output.

The driver registers a net device for each PF it probed.  Net devices are
registered with multiple queues.  The number of queues depends on the number of
MSI-X vectors available through the associated PF.  In particular, for PF0 which
acts as the master PF, the number of queues equals to the number of MSI-X
vectors minus 2, one for card-level error interrupt and one for function-level
user interrupt; for other PFs, it equals to the number of MSI-X vectors minus 1.
Each net device has the same number of TX and RX queues.

For each FPGA card loaded with the OpenNIC shell bitstream, the driver detects
the number of CMAC instances and manages the links accordingly.  Only PF0 can
enable/disable the links.  The default bitstream, when configured with 2 PFs and
2 CMAC instances, maps PF0 to port0 and PF1 to port1.
