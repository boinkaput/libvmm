/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include "virtio/virtio_irq.h"
#include "include/config/virtio_blk.h"
#include "include/config/virtio_config.h"
#include "../virq.h"
#include "../util/util.h"
#include "../libsharedringbuffer/include/shared_ringbuffer.h"
#include "virtio_mmio.h"
#include "virtio_blk_mmio.h"

static virtio_mmio_handler_t blk_mmio_handler;



void virtio_blk_mmio_ack(uint64_t vcpu_id, int irq, void *cookie) {
    // Do nothing
    return;
}

virtio_mmio_handler_t *get_virtio_blk_mmio_handler(void) {
    return &blk_mmio_handler;
}

// @jade: add some giant comments about this file

#define BIT_LOW(n) (1ul<<(n))
#define BIT_HIGH(n) (1ul<<(n - 32 ))

#define REG_RANGE(r0, r1)   r0 ... (r1 - 1)

// @jade, @ericc: These are sDDF specific, belong in a configuration file elsewhere ideally
#define SHMEM_NUM_BUFFERS 256
#define SHMEM_BUF_SIZE 0x1000

// @jade, @ivanv: need to be able to get it from vgic
#define VCPU_ID 0

// the list of virtqueue handlers for this instance of virtio blk layer
static virtqueue_t vqs[1];

void virtio_blk_mmio_reset(void)
{
    vqs[0].ready = 0;
    vqs[0].last_idx = 1;
}

int virtio_blk_mmio_get_device_features(uint32_t *features)
{
    if (blk_mmio_handler.data.Status & VIRTIO_CONFIG_S_FEATURES_OK) {
        printf("VIRTIO BLK|WARNING: driver somehow wants to read device features after FEATURES_OK\n");
    }

    switch (blk_mmio_handler.data.DeviceFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            break;
        // features bits 32 to 63
        case 1:
            *features = BIT_HIGH(VIRTIO_F_VERSION_1);
            break;
        default:
            printf("VIRTIO BLK|INFO: driver sets DeviceFeaturesSel to 0x%x, which doesn't make sense\n", blk_mmio_handler.data.DeviceFeaturesSel);
            return 0;
    }
    return 1;
}

int virtio_blk_mmio_set_driver_features(uint32_t features)
{
    return 1;
}

int virtio_blk_mmio_get_device_config(uint32_t offset, uint32_t *ret_val)
{
    return 1;
}

int virtio_blk_mmio_set_device_config(uint32_t offset, uint32_t val)
{
    return 1;
}

// handle queue notify from the guest VM
static int virtio_blk_mmio_handle_queue_notify()
{
    struct vring *vring = &vqs[0].vring;

    /* read the current guest index */
    uint16_t guest_idx = vring->avail->idx;
    uint16_t idx = vqs[0].last_idx;

    for (; idx != guest_idx; idx++) {
        /* read the head of the descriptor chain */
        uint16_t desc_head = vring->avail->ring[idx % vring->num];

        uint16_t curr_desc_head = desc_head;

        do {
            curr_desc_head = vring->desc[curr_desc_head].next;
        } while (vring->desc[curr_desc_head].flags & VRING_DESC_F_NEXT);

    }

    vqs[0].last_idx = idx;

    return 1;
}

static virtio_mmio_funs_t mmio_funs = {
    .device_reset = virtio_blk_mmio_reset,
    .get_device_features = virtio_blk_mmio_get_device_features,
    .set_driver_features = virtio_blk_mmio_set_driver_features,
    .get_device_config = virtio_blk_mmio_get_device_config,
    .set_device_config = virtio_blk_mmio_set_device_config,
    .queue_notify = virtio_blk_mmio_handle_queue_notify,
};

void virtio_blk_mmio_init()
{    
    blk_mmio_handler.data.DeviceID = DEVICE_ID_VIRTIO_BLOCK;
    blk_mmio_handler.data.VendorID = VIRTIO_MMIO_DEV_VENDOR_ID;
    blk_mmio_handler.funs = &mmio_funs;

    blk_mmio_handler.vqs = vqs;
}