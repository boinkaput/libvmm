#
# Copyright 2021, Breakaway Consulting Pty. Ltd.
# Copyright 2022, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(BOARD)),)
$(error BOARD must be specified)
endif

# Default build directory, pass BUILD_DIR=<dir> to override
BUILD_DIR ?= build
# Default config is a debug build, pass CONFIG=<debug/release/benchmark> to override
CONFIG ?= debug

# @ivanv: Check for dependencies and make sure they are installed/in the path

# @ivanv: check that all dependencies exist
# Specify that we use bash for all shell commands
SHELL=/bin/bash
# All dependencies needed to compile the VMM
QEMU := qemu-system-aarch64
DTC := dtc

ifndef TOOLCHAIN
 	# Get whether the common toolchain triples exist
 	TOOLCHAIN_AARCH64_LINUX_GNU := $(shell command -v aarch64-linux-gnu-gcc 2> /dev/null)
 	TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU := $(shell command -v aarch64-unknown-linux-gnu-gcc 2> /dev/null)
	# Then check if they are defined and select the appropriate one
	ifdef TOOLCHAIN_AARCH64_LINUX_GNU
		TOOLCHAIN := aarch64-linux-gnu
	else ifdef TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU
		TOOLCHAIN := aarch64-unknown-linux-gnu
	else
		$(error "Could not find an AArch64 cross-compiler")
	endif
endif

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

# @ivanv: need to have a step for putting in the initrd node into the DTB,
# 		  right now it is unfortunately hard-coded.

# @ivanv: check that the path of SDK_PATH/BOARD exists
# @ivanv: Have a list of supported boards to check with, if it's not one of those
# have a helpful message that lists all the support boards.

# @ivanv: incremental builds don't work with IMAGE_DIR changing

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
VMM := ../../
VMM_TOOLS := $(VMM)/tools
VMM_SRC_DIR := $(VMM)/src
SYSTEM_DESCRIPTION := board/$(BOARD)/virtio_demo.system

IMAGE_DIR := board/$(BOARD)
LINUX := $(IMAGE_DIR)/linux
CLIENT_DTS := $(IMAGE_DIR)/client_linux.dts
CLIENT_DTB := $(BUILD_DIR)/client_linux.dtb
DRIVER_DTS_SOURCE := $(IMAGE_DIR)/driver_linux.dts
DRIVER_DTS_OVERLAY := $(IMAGE_DIR)/driver_linux_overlay.dts
DRIVER_DTB := $(BUILD_DIR)/driver_linux.dtb
INITRD := $(IMAGE_DIR)/rootfs.cpio.gz

ELFS := driver_vmm.elf
# ELFS := client_vmm.elf driver_vmm.elf

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

# @ivanv: should only compile printf.o in debug
# CLIENT_VMM_OBJS := client_vmm.o printf.o virq.o linux.o guest.o psci.o smc.o fault.o util.o vgic.o vgic_v2.o package_client_guest_images.o tcb.o vcpu.o
# CLIENT_VMM_OBJS += shared_ringbuffer.o virtio_mmio.o virtio_net_mmio.o virtio_blk_mmio.o

DRIVER_VMM_OBJS := driver_vmm.o printf.o virq.o linux.o guest.o psci.o smc.o fault.o util.o vgic.o vgic_v2.o package_driver_guest_images.o tcb.o vcpu.o

# Toolchain flags
# FIXME: For optimisation we should consider providing the flag -mcpu.
# FIXME: We should also consider whether -mgeneral-regs-only should be
# used to avoid the use of the FPU and therefore seL4 does not have to
# context switch the FPU.
# Note we only need -Wno-unused-command-line-argument because in Nix
# passes an extra `--gcc-toolchain` flag which we do not need.
CFLAGS := -mstrict-align \
		  -g3 \
		  -O3 \
		  -ffreestanding \
		  -nostdlib \
		  -Wno-unused-command-line-argument \
		  -Wall -Wno-unused-function -Werror \
		  -I$(VMM_SRC_DIR)/arch/aarch64 -I$(VMM_SRC_DIR) -I$(VMM_SRC_DIR)/util -I$(BOARD_DIR)/include \
		  -DBOARD_$(BOARD) \
		  -DCONFIG_$(CONFIG)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

all: directories $(IMAGE_FILE)

qemu: all
	# @ivanv: check that the amount of RAM given to QEMU is at least the number of RAM that QEMU is setup with for seL4.
	if ! command -v $(QEMU) &> /dev/null; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic

directories:
	$(shell mkdir -p $(BUILD_DIR))

# Unpack rootfs, put the userlevel binary in /root, repack rootfs
# test/virtio_blk_test.elf: test/virtio_blk_test.c Makefile
# 	$(CC) test/virtio_blk_test.c -o test/virtio_blk_test.elf && \
# 	cp $(IMAGE_DIR)/rootfs.cpio.gz ~/TS/rootfs_unpack && \
# 	cd ~/TS/rootfs_unpack && \
# 	rm -r rootfs 2> /dev/null; \
# 	sh unpack.sh > /dev/null 2>&1  && \
# 	cp ~/TS/libvmm/examples/virtio_blk/test/virtio_blk_test.elf rootfs/root && \
# 	sh pack.sh > /dev/null /dev/null 2>&1 && \
# 	cp rootfs.cpio.gz.new ~/TS/libvmm/examples/virtio_blk/$(IMAGE_DIR)/rootfs.cpio.gz

# $(CLIENT_DTB): $(CLIENT_DTS)
# 	if ! command -v $(DTC) &> /dev/null; then echo "Could not find dependency: Device Tree Compiler (dtc)"; exit 1; fi
# 	# @ivanv: Shouldn't supress warnings
# 	$(DTC) -q -I dts -O dtb $< > $@

$(DRIVER_DTB): $(DRIVER_DTS_SOURCE)
	if ! command -v $(DTC) &> /dev/null; then echo "Could not find dependency: Device Tree Compiler (dtc)"; exit 1; fi
	# @ivanv: Shouldn't supress warnings
	$(DTC) -q -I dts -O dtb $< > $@

# $(DRIVER_DTB): $(DRIVER_DTS_SOURCE) $(DRIVER_DTS_OVERLAY) $(BUILD_DIR)/tmp.dts
# 	if ! command -v $(DTC) &> /dev/null; then echo "Could not find dependency: Device Tree Compiler (dtc)"; exit 1; fi
# 	sh ../../scripts/dtscat $(DRIVER_DTS_SOURCE) $(DRIVER_DTS_OVERLAY) > $(BUILD_DIR)/tmp.dts 
# 	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/tmp.dts > $@

# $(BUILD_DIR)/package_client_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(IMAGE_DIR) $(LINUX) $(INITRD) $(CLIENT_DTB)
# 	$(CC) -c -g3 -x assembler-with-cpp \
# 					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
# 					-DGUEST_DTB_IMAGE_PATH=\"$(CLIENT_DTB)\" \
# 					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
# 					$< -o $@

$(BUILD_DIR)/package_driver_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(IMAGE_DIR) $(LINUX) $(INITRD) $(DRIVER_DTB)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(DRIVER_DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					$< -o $@

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/arch/aarch64/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/arch/aarch64/vgic/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/libsharedringbuffer/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/virtio/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# $(BUILD_DIR)/client_vmm.elf: $(addprefix $(BUILD_DIR)/, $(CLIENT_VMM_OBJS))
# 	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/driver_vmm.elf: $(addprefix $(BUILD_DIR)/, $(DRIVER_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(ELFS)) $(SYSTEM_DESCRIPTION) $(IMAGE_DIR)
	$(MICROKIT_TOOL) $(SYSTEM_DESCRIPTION) --search-path $(BUILD_DIR) $(IMAGE_DIR) --board $(BOARD) --config $(CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
	