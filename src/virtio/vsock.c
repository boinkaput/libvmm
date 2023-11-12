#include "virtio/config.h"
#include "virtio/mmio.h"
#include "virtio/vsock.h"
#include "util/util.h"
#include "virq.h"

/* Uncomment this to enable debug logging */
#define DEBUG_VSOCK

#if defined(DEBUG_VSOCK)
#define LOG_VSOCK(...) do{ printf("VIRTIO(VSOCK): "); printf(__VA_ARGS__); }while(0)
#else
#define LOG_VSOCK(...) do{}while(0)
#endif

#define LOG_VSOCK_ERR(...) do{ printf("VIRTIO(VSOCK)|ERROR: "); printf(__VA_ARGS__); }while(0)

// @ivanv: put in util or remove
#define BIT_LOW(n)  (1ul<<(n))
#define BIT_HIGH(n) (1ul<<(n - 32 ))

static void virtio_vsock_features_print(uint32_t features) {
    /* Dump the features given in a human-readable format */
}

static void virtio_vsock_reset(struct virtio_device *dev) {
    LOG_VSOCK("operation: reset\n");
    LOG_VSOCK_ERR("virtio_vsock_reset is not implemented!\n");

    // @ivanv reset vqs?
}

static int virtio_vsock_get_device_features(struct virtio_device *dev, uint32_t *features) {
    LOG_VSOCK("operation: get device features\n");

    switch (dev->data.DeviceFeaturesSel) {
        case 0:
            *features = 0;
            break;
        case 1:
            *features = BIT_HIGH(VIRTIO_F_VERSION_1);
            break;
        default:
            // @ivanv: audit
            LOG_VSOCK_ERR("driver sets DeviceFeaturesSel to 0x%x, which doesn't make sense\n", dev->data.DeviceFeaturesSel);
            return 0;
    }

    return 1;
}

static int virtio_vsock_set_driver_features(struct virtio_device *dev, uint32_t features) {
    LOG_VSOCK("operation: set driver features\n");
    virtio_vsock_features_print(features);

    int success = 1;

    switch (dev->data.DriverFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            // The device initialisation protocol says the driver should read device feature bits,
            // and write the subset of feature bits understood by the OS and driver to the device.
            // Currently we only have one feature to check.
            // success = (features == BIT_LOW(VIRTIO_NET_F_MAC));
            break;
        // features bits 32 to 63
        case 1:
            success = (features == BIT_HIGH(VIRTIO_F_VERSION_1));
            break;
        default:
            LOG_VSOCK_ERR("driver sets DriverFeaturesSel to 0x%x, which doesn't make sense\n", dev->data.DriverFeaturesSel);
            return false;
    }

    if (success) {
        dev->data.features_happy = 1;
        LOG_VSOCK("device is feature happy\n");
    }

    return success;
}

static int virtio_vsock_get_device_config(struct virtio_device *dev, uint32_t offset, uint32_t *config) {
    LOG_VSOCK("operation: get device config\n");
    return -1;
}

static int virtio_vsock_set_device_config(struct virtio_device *dev, uint32_t offset, uint32_t config) {
    LOG_VSOCK("operation: set device config\n");
    return -1;
}

static int virtio_vsock_handle_tx(struct virtio_device *dev) {
    LOG_VSOCK("operation: handle tx\n");
    return -1;
}

int virtio_vsock_handle_rx(struct virtio_device *dev) {
    LOG_VSOCK("operation: handle rx\n");
    return -1;
}

static virtio_device_funs_t functions = {
    .device_reset = virtio_vsock_reset,
    .get_device_features = virtio_vsock_get_device_features,
    .set_driver_features = virtio_vsock_set_driver_features,
    .get_device_config = virtio_vsock_get_device_config,
    .set_device_config = virtio_vsock_set_device_config,
    .queue_notify = virtio_vsock_handle_tx,
};

void virtio_vsock_init(struct virtio_device *dev,
                         struct virtio_queue_handler *vqs, size_t num_vqs,
                         size_t virq,
                         size_t sddf_mux_tx_ch) {
    LOG_VSOCK("initialising\n");
    // @ivanv: check that num_vqs is greater than the minimum vqs to function?
    dev->data.DeviceID = DEVICE_ID_VIRTIO_VSOCK;
    dev->data.VendorID = VIRTIO_MMIO_DEV_VENDOR_ID;
    dev->funs = &functions;
    dev->vqs = vqs;
    dev->num_vqs = num_vqs;
    dev->virq = virq;
    // @ivanv
    dev->sddf_rx_ring = NULL;
    dev->sddf_tx_ring = NULL;
    dev->sddf_mux_tx_ch = sddf_mux_tx_ch;
}
