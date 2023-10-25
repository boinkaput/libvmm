/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include <stdint.h>
// #include "../vgic/vgic.h"
#include "../util/util.h"
#include "include/config/virtio_gpu.h"
#include "include/config/virtio_config.h"
#include "virtio_mmio.h"
#include "virtio_gpu_emul.h"
// #include "virtio_gpu_device.h"
// #include "virtio_gpu_sddf.h"

// virtio gpu mmio emul interface

// @jade, @ivanv: need to be able to get it from vgic
#define VCPU_ID 0

#define BIT_LOW(n) (1ul<<(n))
#define BIT_HIGH(n) (1ul<<(n - 32 ))

#define REG_RANGE(r0, r1)   r0 ... (r1 - 1)

// emul handler for an instance of virtio gpu
static virtio_emul_handler_t gpu_emul_handler;

// virtio-gpu config values
static struct virtio_gpu_config gpu_config;

// the list of virtqueue handlers for an instance of virtio gpu
static virtqueue_t vqs[VIRTIO_MMIO_GPU_NUM_VIRTQUEUE];

// list of created resource ids
// static uint32_t resource_ids[MAX_RESOURCE];

// 2D array of memory entries, indexed with [resource_id][max]
// static struct virtio_gpu_mem_entry mem_entries[MAX_RESOURCE][MAX_MEM_ENTRIES];

void virtio_gpu_ack(uint64_t vcpu_id, int irq, void *cookie) {
    // printf("\"%s\"|VIRTIO GPU|INFO: virtio_gpu_ack %d\n", sel4cp_name, irq);
}

virtio_emul_handler_t *get_virtio_gpu_emul_handler(void)
{
    // san check in case somebody wants to get the handler of an uninitialised backend
    if (gpu_emul_handler.data.VendorID != VIRTIO_MMIO_DEV_VENDOR_ID) {
        return NULL;
    }

    return &gpu_emul_handler;
}

static void virtio_gpu_emul_reset(virtio_emul_handler_t *self)
{
    printf("\"%s\"|VIRTIO GPU|INFO: device has been reset\n", sel4cp_name);
    
    self->data.Status = 0;
    
    vqs[CONTROL_QUEUE].last_idx = 0;
    vqs[CONTROL_QUEUE].ready = 0;

    vqs[CURSOR_QUEUE].last_idx = 0;
    vqs[CURSOR_QUEUE].ready = 0;
}

static int virtio_gpu_emul_get_device_features(virtio_emul_handler_t *self, uint32_t *features)
{
    if (self->data.Status & VIRTIO_CONFIG_S_FEATURES_OK) {
        printf("VIRTIO GPU|WARNING: driver somehow wants to read device features after FEATURES_OK\n");
    }

    switch (self->data.DeviceFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            // No GPU specific features supported
            break;
        // features bits 32 to 63
        case 1:
            *features = BIT_HIGH(VIRTIO_F_VERSION_1);
            break;
        default:
            printf("VIRTIO GPU|INFO: driver sets DeviceFeaturesSel to 0x%x, which doesn't make sense\n", self->data.DeviceFeaturesSel);
            return 0;
    }
    return 1;
}

static int virtio_gpu_emul_set_driver_features(virtio_emul_handler_t *self, uint32_t features)
{
    int success = 1;

    switch (self->data.DriverFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            // The device initialisation protocol says the driver should read device feature bits,
            // and write the subset of feature bits understood by the OS and driver to the device.
            // But no GPU specific features supported.
            break;

        // features bits 32 to 63
        case 1:
            success = (features == BIT_HIGH(VIRTIO_F_VERSION_1));
            break;

        default:
            printf("VIRTIO GPU|INFO: driver sets DriverFeaturesSel to 0x%x, which doesn't make sense\n", self->data.DriverFeaturesSel);
            success = 0;
    }
    if (success) {
        self->data.features_happy = 1;
    }
    return success;
}

static int virtio_gpu_emul_get_device_config(struct virtio_emul_handler *self, uint32_t offset, uint32_t *ret_val)
{
    void * config_base_addr = (void *)&gpu_config;
    uint32_t *config_field_addr = (uint32_t *)(config_base_addr + (offset - REG_VIRTIO_MMIO_CONFIG));
    *ret_val = *config_field_addr;
    // printf("VIRTIO GPU|INFO: get_device_config_field config_field_address 0x%x returns retval %d\n", config_field_addr, *ret_val);
    return 1;
}

static int virtio_gpu_emul_set_device_config(struct virtio_emul_handler *self, uint32_t offset, uint32_t val)
{
    void * config_base_addr = (void *)&gpu_config;
    uint32_t *config_field_addr = (uint32_t *)(config_base_addr + (offset - REG_VIRTIO_MMIO_CONFIG));
    *config_field_addr = val;
    // printf("VIRTIO GPU|INFO: set_device_config_field set 0x%x to %d\n", config_field_addr, val);
    return 1;
}

static int virtio_gpu_emul_handle_queue_notify(struct virtio_emul_handler *self)
{
    // gpu_virtqueue_to_sddf(get_uio_map(), &vqs[CONTROL_QUEUE]);

    // Notify driver VM
    // sel4cp_notify(VIRTIO_GPU_CH);
    
    return 1;
}

static virtio_emul_funs_t gpu_emul_funs = {
    .device_reset = virtio_gpu_emul_reset,
    .get_device_features = virtio_gpu_emul_get_device_features,
    .set_driver_features = virtio_gpu_emul_set_driver_features,
    .get_device_config = virtio_gpu_emul_get_device_config,
    .set_device_config = virtio_gpu_emul_set_device_config,
    .queue_notify = virtio_gpu_emul_handle_queue_notify,
};

static void virtio_gpu_config_init(void) {
    // Hard coded values, need to retrieve these from device.
    gpu_config.events_read = 0;
    gpu_config.events_clear = 0;
    gpu_config.num_scanouts = 1;
    gpu_config.num_capsets = 0;
}

void virtio_gpu_emul_init(void)
{
    virtio_gpu_config_init();
    // virtio_gpu
    
    gpu_emul_handler.data.DeviceID = DEVICE_ID_VIRTIO_GPU;
    gpu_emul_handler.data.VendorID = VIRTIO_MMIO_DEV_VENDOR_ID;
    gpu_emul_handler.funs = &gpu_emul_funs;

    gpu_emul_handler.vqs = vqs;
}