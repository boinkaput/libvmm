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

# Specify that we use bash for all shell commands
SHELL=/bin/bash
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

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
VMM := ../../
VMM_TOOLS := $(VMM)/tools
VMM_SRC_DIR := $(VMM)/src
SYSTEM_DESCRIPTION := board/$(BOARD)/virtio.system

DRIVER_DIR := board/$(BOARD)/driver
CLIENT_DIR := board/$(BOARD)/client
DRIVER_DTS_DIR := $(DRIVER_DIR)/dts
CLIENT_DTS_DIR := $(CLIENT_DIR)/dts

DRIVER_USERLEVEL_DIR := userlevel/driver
CLIENT_USERLEVEL_DIR := userlevel/client

DRIVER_ROOTFS_TMP_DIR := $(BUILD_DIR)/driver_rootfs
CLIENT_ROOTFS_TMP_DIR := $(BUILD_DIR)/client_rootfs

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

DRIVER_LINUX := $(DRIVER_DIR)/linux
DRIVER_INITRD := $(DRIVER_DIR)/rootfs.cpio.gz
DRIVER_BASE_DTS := $(DRIVER_DTS_DIR)/linux.dts
DRIVER_DTS_OVERLAYS := $(DRIVER_DTS_DIR)/init.dts $(DRIVER_DTS_DIR)/uio.dts

CLIENT_LINUX := $(CLIENT_DIR)/linux
CLIENT_INITRD := $(CLIENT_DIR)/rootfs.cpio.gz
CLIENT_BASE_DTS := $(CLIENT_DTS_DIR)/linux.dts
CLIENT_DTS_OVERLAYS := $(CLIENT_DTS_DIR)/init.dts $(CLIENT_DTS_DIR)/virtio_blk.dts 

DRIVER_DTB := $(BUILD_DIR)/driver_linux.dtb
CLIENT_DTB := $(BUILD_DIR)/client_linux.dtb

VMM_OBJS := printf.o linux.o guest.o psci.o smc.o fault.o util.o vgic.o tcb.o vcpu.o vgic_v2.o virq.o
CLIENT_VMM_OBJS := $(VMM_OBJS) client_guest_vmm.o client_guest_images.o shared_ringbuffer.o virtio_mmio.o virtio_net_mmio.o virtio_blk_mmio.o
DRIVER_VMM_OBJS := $(VMM_OBJS) driver_guest_vmm.o driver_guest_images.o

ELFS := client_guest_vmm.elf driver_guest_vmm.elf

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

DRIVER_USERLEVEL_ELFS := cmdq_test.elf
CLIENT_USERLEVEL_ELFS := virtio_blk_test.elf

all: directories $(IMAGE_FILE)
# all: directories driver_userlevel client_userlevel $(IMAGE_FILE) 

directories:
	$(shell mkdir -p $(BUILD_DIR))

qemu: all
	# @ivanv: check that the amount of RAM given to QEMU is at least the number of RAM that QEMU is setup with for seL4.
	if ! command -v $(QEMU) &> /dev/null; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic

$(BUILD_DIR)/%.elf: $(DRIVER_USERLEVEL_DIR)/%.c Makefile
	$(CC) $< -o $@

$(BUILD_DIR)/%.elf: $(CLIENT_USERLEVEL_DIR)/%.c Makefile
	$(CC) $< -o $@

# Unpack rootfs, put the userlevel binary in /root, repack rootfs
driver_userlevel: $(addprefix $(BUILD_DIR)/, $(DRIVER_USERLEVEL_ELFS))
	rm -r $(DRIVER_ROOTFS_TMP_DIR) 2> /dev/null; \
	mkdir -p $(DRIVER_ROOTFS_TMP_DIR) && \
	gunzip -dc $(DRIVER_DIR)/rootfs.cpio.gz | cpio -idmv -D$(DRIVER_ROOTFS_TMP_DIR) && \
	cp $< $(DRIVER_ROOTFS_TMP_DIR)/root && \
	find $(DRIVER_ROOTFS_TMP_DIR) | cpio -o -H newc -R root:root | gzip > $(DRIVER_DIR)/rootfs.cpio.gz
	
client_userlevel: $(addprefix $(BUILD_DIR)/, $(CLIENT_USERLEVEL_ELFS))
	rm -r $(CLIENT_ROOTFS_TMP_DIR) 2> /dev/null; \
	mkdir -p $(CLIENT_ROOTFS_TMP_DIR) && \
	gunzip -dc $(CLIENT_DIR)/rootfs.cpio.gz | cpio -idmv -D$(CLIENT_ROOTFS_TMP_DIR) && \
	cp $< $(CLIENT_ROOTFS_TMP_DIR)/root && \
	find $(CLIENT_ROOTFS_TMP_DIR) | cpio -o -H newc -R root:root | gzip > $(CLIENT_DIR)/rootfs.cpio.gz

$(CLIENT_DTB): $(CLIENT_BASE_DTS) $(CLIENT_DTS_OVERLAYS)
	../../scripts/dtscat $^ > $(BUILD_DIR)/client_guest.dts
	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/client_guest.dts > $@

$(BUILD_DIR)/client_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(CLIENT_LINUX) $(CLIENT_DTB) $(CLIENT_INITRD)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(CLIENT_LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(CLIENT_DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(CLIENT_INITRD)\" \
					$< -o $@

$(DRIVER_DTB): $(DRIVER_BASE_DTS) $(DRIVER_DTS_OVERLAYS)
	../../scripts/dtscat $^ > $(BUILD_DIR)/driver_guest.dts
	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/driver_guest.dts > $@

$(BUILD_DIR)/driver_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(DRIVER_LINUX) $(DRIVER_DTB) $(DRIVER_INITRD)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(DRIVER_LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(DRIVER_DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(DRIVER_INITRD)\" \
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

$(BUILD_DIR)/client_guest_vmm.elf: $(addprefix $(BUILD_DIR)/, $(CLIENT_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/driver_guest_vmm.elf: $(addprefix $(BUILD_DIR)/, $(DRIVER_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(ELFS)) $(SYSTEM_DESCRIPTION) $(DRIVER_DIR) $(CLIENT_DIR)
	$(MICROKIT_TOOL) $(SYSTEM_DESCRIPTION) --search-path $(BUILD_DIR) $(DRIVER_DIR) $(CLIENT_DIR) --board $(BOARD) --config $(CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
