# 
# Copyright (c) 2020 Xilinx, Inc.
# All rights reserved.
# 
# This source code is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
# 
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
# 
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# ifdef KVERSION
# KERNEL_VERS = $(KVERSION)
# else
# KERNEL_VERS = $(shell uname -r)
# endif

# srcdir = $(PWD)
# obj-m += onic.o
# BASE_OBJS := $(patsubst $(srcdir)/%.c,%.o,$(wildcard $(srcdir)/*.c $(srcdir)/*/*.c $(srcdir)/*/*/*.c))
# onic-objs = $(BASE_OBJS)
# ccflags-y = -O3 -Wall -Werror -I$(srcdir)/qdma_access -I$(srcdir)/hwmon -I$(srcdir)

# KDIR ?= /lib/modules/$(KERNEL_VERS)/build

# all:
# 	make -C $(KDIR) M=$(PWD) modules

# with-clang:
# 	make CC=clang -C $(KDIR) M=$(PWD) modules
	
# clean:
# 	make -C $(KDIR) M=$(PWD) clean
# 	rm -f *.o.ur-safe
# 	rm -f ./qdma_access/*.o.ur-safe

# install:
# 	rm -f /lib/modules/$(KERNEL_VERS)/onic.ko
# 	cp onic.ko /lib/modules/$(KERNEL_VERS)
# 	depmod

# uninstall:
# 	rm -f /lib/modules/$(KERNEL_VERS)/onic.ko
# 	depmod


# ****************************************************************

# ifdef KVERSION
# KERNEL_VERS = $(KVERSION)
# else
# KERNEL_VERS = $(shell uname -r)
# endif

# srcdir = $(PWD)
# BUILDDIR ?= $(srcdir)/build

# obj-m += onic.o

# BASE_OBJS := $(patsubst $(srcdir)/%.c,%.o,$(wildcard $(srcdir)/*.c $(srcdir)/*/*.c $(srcdir)/*/*/*.c))

# onic-objs = $(BASE_OBJS)

# ccflags-y = -O3 -Wall -Werror \
# 	-I$(srcdir)/qdma_access \
# 	-I$(srcdir)/hwmon \
# 	-I$(srcdir)

# KDIR ?= /lib/modules/$(KERNEL_VERS)/build

# all:
# 	mkdir -p $(BUILDDIR)
# 	make -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) modules

# with-clang:
# 	mkdir -p $(BUILDDIR)
# 	make CC=clang -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) modules

# clean:
# 	make -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) clean
# 	rm -rf $(BUILDDIR)

# install:
# 	rm -f /lib/modules/$(KERNEL_VERS)/onic.ko
# 	cp $(BUILDDIR)/onic.ko /lib/modules/$(KERNEL_VERS)
# 	depmod

# uninstall:
# 	rm -f /lib/modules/$(KERNEL_VERS)/onic.ko
# 	depmod


ifdef KVERSION
KERNEL_VERS = $(KVERSION)
else
KERNEL_VERS = $(shell uname -r)
endif

# srcdir := $(CURDIR) # for laptop
srcdir := $(PWD) # access for linux kernel server
BUILDDIR ?= $(srcdir)/build
KDIR ?= /lib/modules/$(KERNEL_VERS)/build

obj-m += onic_pf.o
obj-m += onic_vf.o

# PF driver objects
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

# VF driver objects
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

ccflags-y := -O3 -Wall -Werror \
	-I$(srcdir) \
	-I$(srcdir)/qdma_access \
	-I$(srcdir)/hwmon

all:
	mkdir -p $(BUILDDIR)
	make -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) modules

with-clang:
	mkdir -p $(BUILDDIR)
	make CC=clang -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) modules

clean:
	make -C $(KDIR) M=$(srcdir) MO=$(BUILDDIR) clean
	rm -rf $(BUILDDIR)

install:
	cp $(BUILDDIR)/onic_pf.ko /lib/modules/$(KERNEL_VERS)/
	cp $(BUILDDIR)/onic_vf.ko /lib/modules/$(KERNEL_VERS)/
	depmod

uninstall:
	rm -f /lib/modules/$(KERNEL_VERS)/onic_pf.ko
	rm -f /lib/modules/$(KERNEL_VERS)/onic_vf.ko
	depmod