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
SYSTEM_DESCRIPTION := board/$(BOARD)/uio.system

IMAGE_DIR := board/$(BOARD)
LINUX := $(IMAGE_DIR)/linux
INITRD := $(IMAGE_DIR)/rootfs.cpio.gz

BASE_DTS := $(IMAGE_DIR)/linux.dts
# @tim: figure out best way to specify DTS overlays
UIO_DTS := $(IMAGE_DIR)/uio.dts
ETH_DTS := $(IMAGE_DIR)/eth.dts
DRIVER_DTS_OVERLAYS := $(UIO_DTS) $(ETH_DTS)

DISABLE_HW_DTS := $(IMAGE_DIR)/disable_hw.dts
CLIENT_DTS_OVERLAYS := $(DISABLE_HW_DTS) 

CLIENT_DTB := $(BUILD_DIR)/client_linux.dtb
DRIVER_DTB := $(BUILD_DIR)/driver_linux.dtb

ELFS := client_guest_vmm.elf driver_guest_vmm.elf

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

# @ivanv: should only compile printf.o in debug
VMM_OBJS := printf.o linux.o guest.o psci.o smc.o fault.o util.o vgic.o tcb.o vcpu.o vgic_v2.o virq.o
CLIENT_VMM_OBJS := $(VMM_OBJS) client_guest_vmm.o client_guest_images.o shared_ringbuffer.o virtio_mmio.o virtio_net_mmio.o virtio_blk_mmio.o
DRIVER_VMM_OBJS := $(VMM_OBJS) driver_guest_vmm.o driver_guest_images.o

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

directories:
	$(shell mkdir -p $(BUILD_DIR))

$(CLIENT_DTB): $(BASE_DTS) $(CLIENT_DTS_OVERLAYS)
	../../scripts/dtscat $^ > $(BUILD_DIR)/client_guest.dts
	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/client_guest.dts > $@

$(BUILD_DIR)/client_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(IMAGE_DIR) $(LINUX) $(CLIENT_DTB) $(INITRD)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(CLIENT_DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					$< -o $@

$(DRIVER_DTB): $(BASE_DTS) $(DRIVER_DTS_OVERLAYS)
	../../scripts/dtscat $^ > $(BUILD_DIR)/driver_guest.dts
	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/driver_guest.dts > $@

$(BUILD_DIR)/driver_guest_images.o: $(VMM_TOOLS)/package_guest_images.S $(IMAGE_DIR) $(LINUX) $(DRIVER_DTB) $(INITRD)
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

$(BUILD_DIR)/client_guest_vmm.elf: $(addprefix $(BUILD_DIR)/, $(CLIENT_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/driver_guest_vmm.elf: $(addprefix $(BUILD_DIR)/, $(DRIVER_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(ELFS)) $(SYSTEM_DESCRIPTION) $(IMAGE_DIR)
	$(MICROKIT_TOOL) $(SYSTEM_DESCRIPTION) --search-path $(BUILD_DIR) $(IMAGE_DIR) --board $(BOARD) --config $(CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
