#
# Copyright 2021, Breakaway Consulting Pty. Ltd.
# Copyright 2022, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

ifeq ($(strip $(SEL4CP_BOARD)),)
$(error SEL4CP_BOARD must be specified)
endif

ifeq ($(strip $(SEL4CP_CONFIG)),)
$(error SEL4CP_CONFIG must be specified)
endif

ifeq ($(strip $(SYSTEM)),)
$(error SYSTEM must be specified)
endif

# @ivanv: Check for dependencies and make sure they are installed/in the path

# @ivanv: check that all dependencies exist
SHELL=/bin/bash
QEMU := qemu-system-aarch64
AARCH64_TOOLCHAIN := aarch64-linux-gnu
RISCV_TOOLCHAIN := riscv64-linux-gnu
DTC := dtc
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp

# ARCH := aarch64

# ifeq ($(SEL4CP_BOARD),qemu_riscv_virt)
# 	TOOLCHAIN = riscv64-unknown-elf
#     ARCH = riscv64
# endif

CPU := cortex-a53

VMM_OBJS := vmm.o printf.o util.o global_data.o

AARCH64_OBJS := psci.o smc.o fault.o vgic.o

# @ivanv: hack...
# This step should be done based on the DTB
# ifeq ($(SEL4CP_BOARD),imx8mm_evk)
# 	VMM_OBJS += vgic_v3.o
# else
# 	VMM_OBJS += vgic_v2.o
# endif

# @ivanv: need to have a step for putting in the initrd node into the DTB,
# 		  right now it is unfortunately hard-coded.

# @ivanv: check that the path of SDK_PATH/BOARD exists
# @ivanv: Have a list of supported boards to check with, if it's not one of those
# have a helpful message that lists all the support boards.

SEL4CP_BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)
SRC_DIR := src
IMAGE_DIR := board/$(SEL4CP_BOARD)/images
SYSTEM_DESCRIPTION := board/$(SEL4CP_BOARD)/systems/$(SYSTEM)

KERNEL_IMAGE := $(IMAGE_DIR)/linux
DTB_SOURCE := $(IMAGE_DIR)/linux.dts
DTB_IMAGE := linux.dtb
INITRD_IMAGE := $(IMAGE_DIR)/rootfs.cpio.gz

LINUX_IMAGES := $(DTB_IMAGE)

IMAGES := $(LINUX_IMAGES) vmm.elf

# @ivanv: compiling with -O3, the compiler complains about missing memset
CFLAGS := -mstrict-align -nostdlib -ffreestanding -g3 -Wall -Wno-unused-function -Werror -I$(SEL4CP_BOARD_DIR)/include -DBOARD_$(SEL4CP_BOARD) -DARCH_$(ARCH) -DCONFIG_$(SEL4CP_CONFIG)
LDFLAGS := -L$(SEL4CP_BOARD_DIR)/lib
LIBS := -lsel4cp -Tsel4cp.ld

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt
OPENSBI_PAYLOAD := $(BUILD_DIR)/platform/generic/firmware/fw_payload.elf

ifeq ($(ARCH),aarch64)
	VMM_OBJS += $(AARCH64_OBJS)
	TOOLCHAIN := $(AARCH64_TOOLCHAIN)
	CFLAGS +=  -mcpu=$(CPU) -DPRINTF_DISABLE_SUPPORT_FLOAT
	ASM_FLAGS := -mcpu=$(CPU)
else
	TOOLCHAIN := $(RISCV_TOOLCHAIN)
	CFLAGS += -march=rv64imac -mabi=lp64 -mcmodel=medany -DPRINTF_DISABLE_SUPPORT_FLOAT
	ASM_FLAGS += -march=rv64imac -mabi=lp64
endif

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as

# @ivanv: incremental building with the IMAGE_DIR set does not work.
all: directories $(BUILD_DIR)/$(DTB_IMAGE) $(KERNEL_IMAGE) $(INITRD_IMAGE) $(IMAGE_FILE)

directories:
	$(shell mkdir -p $(BUILD_DIR))

run: all $(IMAGE_FILE)
	# @ivanv: check that the amount of RAM given to QEMU is at least the number of RAM that QEMU is setup with for seL4.
	$(QEMU) -machine virt,virtualization=on,highmem=off,secure=off \
			-cpu $(CPU) \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic

$(BUILD_DIR)/$(DTB_IMAGE): $(DTB_SOURCE)
	if ! command -v $(DTC) &> /dev/null; then echo "You need a Device Tree Compiler (dtc) installed"; exit 1; fi
	# @ivanv: Shouldn't supress warnings
	$(DTC) -q -I dts -O dtb $< > $@

$(BUILD_DIR)/global_data.o: $(SRC_DIR)/global_data.S
	$(CC) -c -g3 -x assembler-with-cpp $(ASM_FLAGS) \
					-DVM_KERNEL_IMAGE_PATH=\"$(KERNEL_IMAGE)\" \
					-DVM_DTB_IMAGE_PATH=\"$(BUILD_DIR)/linux.dtb\" \
					-DVM_INITRD_IMAGE_PATH=\"$(INITRD_IMAGE)\" \
					$< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# $(BUILD_DIR)/%.o: $(SRC_DIR)/vgic/%.c Makefile
# 	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vmm.elf: $(addprefix $(BUILD_DIR)/, $(VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) $(SYSTEM_DESCRIPTION)
	$(SEL4CP_TOOL) $(SYSTEM_DESCRIPTION) --search-path $(BUILD_DIR) $(IMAGE_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
