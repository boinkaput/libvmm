/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "virtio/virtio_mmio.h"

#define VIRTIO_BLK_MMIO_NUM_VIRTQUEUE   2

void virtio_blk_mmio_ack(uint64_t vcpu_id, int irq, void *cookie);

virtio_mmio_handler_t *get_virtio_blk_mmio_handler(void);

void virtio_blk_mmio_init();
