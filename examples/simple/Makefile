#
# Copyright 2021, Breakaway Consulting Pty. Ltd.
# Copyright 2022, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK := $(abspath ${MICROKIT_SDK})
ifeq ($(strip $(BOARD)),)
$(error BOARD must be specified)
endif

ifeq ($(strip ${SDDF}),)
$(error SDDF must be specified)
endif
override SDDF := $(abspath ${SDDF})
export SDDF
export MICROKIT_SDK

export EXAMPLE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Default build directory, pass BUILD_DIR=<dir> to override
BUILD_DIR ?= build
# Default config is a debug build, pass CONFIG=<debug/release/benchmark> to override
CONFIG ?= debug


# @ivanv: Check for dependencies and make sure they are installed/in the path

# @ivanv: check that all dependencies exist
# Specify that we use POSIX shell for all shell commands
SHELL := /bin/sh
# All dependencies needed to compile the VMM
QEMU := qemu-system-aarch64
DTC := dtc
IMAGE_FILE := ${BUILD_DIR}/vmm.elf

# ifndef TOOLCHAIN
# Get whether the common toolchain triples exist
# 	TOOLCHAIN_AARCH64_LINUX_GNU := $(shell command -v aarch64-linux-gnu-gcc 2> /dev/null)
# 	TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU := $(shell command -v aarch64-unknown-linux-gnu-gcc 2> /dev/null)
# 	# Then check if they are defined and select the appropriate one
# 	ifdef TOOLCHAIN_AARCH64_LINUX_GNU
# 		TOOLCHAIN := aarch64-linux-gnu
# 	else ifdef TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU
# 		TOOLCHAIN := aarch64-unknown-linux-gnu
# 	else
# 		$(error "Could not find an AArch64 cross-compiler")
# 	endif
# endif

CC := clang
LD := ld.lld

# @ivanv: need to have a step for putting in the initrd node into the DTB,
# 		  right now it is unfortunately hard-coded.

# @ivanv: check that the path of SDK_PATH/BOARD exists
# @ivanv: Have a list of supported boards to check with, if it's not one of those
# have a helpful message that lists all the support boards.

# @ivanv: incremental builds don't work with IMAGE_DIR changing

export VMM := $(abspath ../../)

all: $(IMAGE_FILE)

qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} SDDF=${SDDF} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: vmm.mk
	mkdir -p ${BUILD_DIR}
	cp vmm.mk $@

FORCE:
