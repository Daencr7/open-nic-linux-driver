ifdef KVERSION
KERNEL_VERS = $(KVERSION)
else
KERNEL_VERS = $(shell uname -r)
endif

# Current source directory
srcdir := $(CURDIR)

# Build output directory
BUILDDIR := $(srcdir)/build

# Linux kernel build directory
KDIR := /lib/modules/$(KERNEL_VERS)/build

obj-m += onic_pf.o
obj-m += onic_vf.o

# =========================================================
# PF driver objects
# =========================================================
onic_pf-objs := \
	onic_common.o \
	onic_ethtool.o \
	onic_hardware.o \
	onic_lib.o \
	onic_main.o \
	onic_mbox.o \
	onic_netdev.o \
	onic_sriov.o \
	onic_sysfs.o \
	hwmon/xmc.o \
	hwmon/xocl_ctx.o \
	hwmon/xocl_debug.o \
	qdma_access/qdma_context.o \
	qdma_access/qdma_device.o \
	qdma_access/qdma_export.o

# =========================================================
# VF driver objects
# =========================================================
onic_vf-objs := \
	onic_common.o \
	onic_vf_main.o \
	onic_vf_hw.o \
	onic_vf_mbox.o \
	onic_vf_netdev.o \
	onic_vf_qdma.o \
	qdma_access/qdma_context.o \
	qdma_access/qdma_device.o \
	qdma_access/qdma_export.o

# =========================================================
# Compiler flags
# =========================================================
ccflags-y := -O3 -Wall -Werror
ccflags-y += -I$(src)
ccflags-y += -I$(src)/qdma_access
ccflags-y += -I$(src)/hwmon

# =========================================================
# Build
# =========================================================
all:
	mkdir -p $(BUILDDIR)
	$(MAKE) -C $(KDIR) M=$(srcdir) modules

# Build with clang
with-clang:
	mkdir -p $(BUILDDIR)
	$(MAKE) CC=clang -C $(KDIR) M=$(srcdir) modules

# =========================================================
# Clean
# =========================================================
clean:
	$(MAKE) -C $(KDIR) M=$(srcdir) clean
	rm -rf $(BUILDDIR)

# =========================================================
# Install
# =========================================================
install:
	cp onic_pf.ko /lib/modules/$(KERNEL_VERS)/
	cp onic_vf.ko /lib/modules/$(KERNEL_VERS)/
	depmod

# =========================================================
# Uninstall
# =========================================================
uninstall:
	rm -f /lib/modules/$(KERNEL_VERS)/onic_pf.ko
	rm -f /lib/modules/$(KERNEL_VERS)/onic_vf.ko
	depmod