/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include "include/config/virtio_blk.h"
#include "include/config/virtio_config.h"
#include "virtio_mmio.h"
#include "virtio_blk_mmio.h"
#include "virtio_irq.h"
#include "../virq.h"
#include "../util/util.h"
#include "../libsharedringbuffer/include/shared_ringbuffer.h"

static virtio_mmio_handler_t blk_mmio_handler;

static struct virtio_blk_config blk_config;

#define BIT_LOW(n) (1ul<<(n))
#define BIT_HIGH(n) (1ul<<(n - 32 ))

#define REG_RANGE(r0, r1)   r0 ... (r1 - 1)

// @jade, @ericc: These are sDDF specific, belong in a configuration file elsewhere ideally
#define SHMEM_NUM_BUFFERS 256
#define SHMEM_BUF_SIZE 0x1000

// @jade, @ivanv: need to be able to get it from vgic
#define VCPU_ID 0

// the list of virtqueue handlers for this instance of virtio blk layer
static virtqueue_t vqs[VIRTIO_BLK_MMIO_NUM_VIRTQUEUE];

void virtio_blk_mmio_ack(uint64_t vcpu_id, int irq, void *cookie) {
    // Do nothing, virtio devices are virtual so ack is not required
    return;
}

virtio_mmio_handler_t *get_virtio_blk_mmio_handler(void) {
    return &blk_mmio_handler;
}

static void virtio_blk_mmio_reset(void)
{
    vqs[DEFAULT_QUEUE].ready = 0;
    vqs[DEFAULT_QUEUE].last_idx = 0;
}

static int virtio_blk_mmio_get_device_features(uint32_t *features)
{
    if (blk_mmio_handler.data.Status & VIRTIO_CONFIG_S_FEATURES_OK) {
        printf("VIRTIO BLK|WARNING: driver somehow wants to read device features after FEATURES_OK\n");
    }

    switch (blk_mmio_handler.data.DeviceFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            *features = BIT_LOW(VIRTIO_BLK_F_FLUSH);
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

static int virtio_blk_mmio_set_driver_features(uint32_t features)
{
    // According to virtio initialisation protocol,
    // this should check what device features were set, and return the subset of features understood
    // by the driver. However, for now we ignore what the driver sets, and just return the features we support.
    int success = 1;

    switch (blk_mmio_handler.data.DriverFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            success = (features & BIT_LOW(VIRTIO_BLK_F_FLUSH));
            break;
        // features bits 32 to 63
        case 1:
            success = (features == BIT_HIGH(VIRTIO_F_VERSION_1));
            break;
        default:
            printf("VIRTIO BLK|INFO: driver sets DriverFeaturesSel to 0x%x, which doesn't make sense\n", blk_mmio_handler.data.DriverFeaturesSel);
            success = 0;
    }

    if (success) {
        blk_mmio_handler.data.features_happy = 1;
    }

    return success;
}

static int virtio_blk_mmio_get_device_config(uint32_t offset, uint32_t *ret_val)
{
    uintptr_t config_base_addr = (uintptr_t)&blk_config;
    uintptr_t config_field_offset = (uintptr_t)(offset - REG_VIRTIO_MMIO_CONFIG);
    uint32_t *config_field_addr = (uint32_t *)(config_base_addr + config_field_offset);
    *ret_val = *config_field_addr;
    // printf("VIRTIO BLK|INFO: get device config with base_addr 0x%x and field_address 0x%x has value %d\n", config_base_addr, config_field_addr, *ret_val);
    return 1;
}

static int virtio_blk_mmio_set_device_config(uint32_t offset, uint32_t val)
{
    uintptr_t config_base_addr = (uintptr_t)&blk_config;
    uintptr_t config_field_offset = (uintptr_t)(offset - REG_VIRTIO_MMIO_CONFIG);
    uint32_t *config_field_addr = (uint32_t *)(config_base_addr + config_field_offset);
    *config_field_addr = val;
    // printf("VIRTIO BLK|INFO: set device config with base_addr 0x%x and field_address 0x%x with value %d\n", config_base_addr, config_field_addr, val);
    return 1;
}

static int virtio_blk_mmio_queue_notify()
{
    virtqueue_t *vq = &vqs[DEFAULT_QUEUE];
    struct vring *vring = &vq->vring;

    /* read the current guest index */
    uint16_t guest_idx = vring->avail->idx;
    uint16_t idx = vq->last_idx;

    printf("\"%s\"|VIRTIO BLK|INFO: ------------- Driver notified device -------------\n", microkit_name);

    for (; idx != guest_idx; idx++) {
        uint16_t desc_head = vring->avail->ring[idx % vring->num];

        uint16_t curr_desc_head = desc_head;

        // printf("\"%s\"|VIRTIO BLK|INFO: Descriptor index is %d, Descriptor flags are: 0x%x, length is 0x%x\n", microkit_name, curr_desc_head, (uint16_t)vring->desc[curr_desc_head].flags, vring->desc[curr_desc_head].len);

        // Print out what the command type is
        struct virtio_blk_outhdr *cmd = (void *)vring->desc[curr_desc_head].addr;
        printf("\"%s\"|VIRTIO BLK|INFO: ----- Command type is 0x%x -----\n", microkit_name, cmd->type);
        
        // Parse different commands
        switch (cmd->type) {
            // header -> body -> reply
            case VIRTIO_BLK_T_IN: {
                printf("\"%s\"|VIRTIO BLK|INFO: Command type is VIRTIO_BLK_T_IN\n", microkit_name);
                printf("\"%s\"|VIRTIO BLK|INFO: Sector (read/write offset) is %d (x512)\n", microkit_name, cmd->sector);
                curr_desc_head = vring->desc[curr_desc_head].next;
                printf("\"%s\"|VIRTIO BLK|INFO: Descriptor index is %d, Descriptor flags are: 0x%x, length is 0x%x\n", microkit_name, curr_desc_head, (uint16_t)vring->desc[curr_desc_head].flags, vring->desc[curr_desc_head].len);
                uintptr_t data = vring->desc[curr_desc_head].addr;
                printf("\"%s\"|VIRTIO BLK|INFO: Data is %s\n", microkit_name, (char *)data);
                break;
            }
            case VIRTIO_BLK_T_OUT: {
                printf("\"%s\"|VIRTIO BLK|INFO: Command type is VIRTIO_BLK_T_OUT\n", microkit_name);
                printf("\"%s\"|VIRTIO BLK|INFO: Sector (read/write offset) is %d (x512)\n", microkit_name, cmd->sector);
                curr_desc_head = vring->desc[curr_desc_head].next;
                printf("\"%s\"|VIRTIO BLK|INFO: Descriptor index is %d, Descriptor flags are: 0x%x, length is 0x%x\n", microkit_name, curr_desc_head, (uint16_t)vring->desc[curr_desc_head].flags, vring->desc[curr_desc_head].len);
                uintptr_t data = vring->desc[curr_desc_head].addr;
                printf("\"%s\"|VIRTIO BLK|INFO: Data is %s\n", microkit_name, (char *)data);
                break;
            }
            case VIRTIO_BLK_T_FLUSH: {
                printf("\"%s\"|VIRTIO BLK|INFO: Command type is VIRTIO_BLK_T_FLUSH\n", microkit_name);
                break;
            }
        }

        // For descriptor chains with more than one descriptor, the last descriptor has the VRING_DESC_F_NEXT flag set to 0
        // to indicate that it is the last descriptor in the chain. That descriptor does not seem to serve any other purpose.
        // This loop brings us to the last descriptor in the chain.
        while (vring->desc[curr_desc_head].flags & VRING_DESC_F_NEXT) {
            // printf("\"%s\"|VIRTIO BLK|INFO: Descriptor index is %d, Descriptor flags are: 0x%x, length is 0x%x\n", microkit_name, curr_desc_head, (uint16_t)vring->desc[curr_desc_head].flags, vring->desc[curr_desc_head].len);
            curr_desc_head = vring->desc[curr_desc_head].next;
        }
        // printf("\"%s\"|VIRTIO BLK|INFO: Descriptor index is %d, Descriptor flags are: 0x%x, length is 0x%x\n", microkit_name, curr_desc_head, (uint16_t)vring->desc[curr_desc_head].flags, vring->desc[curr_desc_head].len);

        // Respond OK for this command to the driver
        // by writing VIRTIO_BLK_S_OK to the final descriptor address
        *((uint8_t *)vring->desc[curr_desc_head].addr) = VIRTIO_BLK_S_OK;



        

        // set the reason of the irq
        blk_mmio_handler.data.InterruptStatus = BIT_LOW(0);

        struct vring_used_elem used_elem = {desc_head, 0};
        uint16_t guest_idx = vring->used->idx;

        vring->used->ring[guest_idx % vring->num] = used_elem;
        vring->used->idx++;

        bool success = virq_inject(VCPU_ID, VIRTIO_BLK_IRQ);
        assert(success);
    }

    vq->last_idx = idx;
    
    return 1;
}

static virtio_mmio_funs_t mmio_funs = {
    .device_reset = virtio_blk_mmio_reset,
    .get_device_features = virtio_blk_mmio_get_device_features,
    .set_driver_features = virtio_blk_mmio_set_driver_features,
    .get_device_config = virtio_blk_mmio_get_device_config,
    .set_device_config = virtio_blk_mmio_set_device_config,
    .queue_notify = virtio_blk_mmio_queue_notify,
};

static void virtio_blk_config_init() 
{
    blk_config.capacity = VIRTIO_BLK_CAPACITY;
}

void virtio_blk_mmio_init()
{    
    blk_mmio_handler.data.DeviceID = DEVICE_ID_VIRTIO_BLOCK;
    blk_mmio_handler.data.VendorID = VIRTIO_MMIO_DEV_VENDOR_ID;
    blk_mmio_handler.funs = &mmio_funs;
    virtio_blk_config_init();

    blk_mmio_handler.vqs = vqs;
}