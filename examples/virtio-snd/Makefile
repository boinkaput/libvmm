#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= build
export MICROKIT_CONFIG ?= benchmark

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
export override MICROKIT_SDK:=$(abspath $(MICROKIT_SDK))

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

export BUILD_DIR:=$(abspath $(BUILD_DIR))
export MICROKIT_SDK:=$(abspath $(MICROKIT_SDK))
export EXAMPLE_DIR:=$(abspath .)

export TARGET := aarch64-none-elf
export TARGET_USERLEVEL := aarch64-linux-gnu
export CC := clang
export CC_USERLEVEL := zig cc
export LD := ld.lld
export AS := llvm-as
export AR := llvm-ar
export DTC := dtc
export RANLIB := llvm-ranlib
export MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
export SDDF=$(abspath ../../dep/sddf)
export LIBVMM=$(abspath ../../)
export LINUX_TOOLS_DIR := $(LIBVMM)/tools/linux

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: $(IMAGE_FILE)

qemu $(IMAGE_FILE) $(REPORT_FILE) clean clobber: $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR) MICROKIT_SDK=$(MICROKIT_SDK) $(notdir $@)

$(BUILD_DIR)/Makefile: virtio-snd.mk
	mkdir -p $(BUILD_DIR)
	cp virtio-snd.mk $@

FORCE:
