/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
* This file is meant to convert virtio virtqueue structures into sDDF shared ringbuffer structures.
* However, it currently implements a vswitch that connects two vmm instances together, and ideally should talk
* to a net multiplexer to determine who to send the packet to. A major refactor is needed to move
* multiplexer logic out of this file.
*/

/**
 * @brief virtio net vswitch implementation
 *
 * - TX: transfers virtio net virtqueue data to sDDF transport layer, and is
 *       delivered to the destination VM using sharedringbuffer
 * - RX: receives data from the source VM via sharedringbuffer and invokes handler
 *       to convert the data from sDDF transport layer to virtio net virtqueue data
 * - Initialisation of virtio_net and vswitch implementation
 *
 * Data flow:
 *
 *          vq                      shringbuf                      vq
 *  src VM <--> (virtio net mmio) <-----------> (virtio net mmio) <--> dest VM
 *                  src VMM                          dest VMM
 *
 * @todo This implementation relies on a connection topology provided by maybe a build system,
 * currently we haven't figured out what to do.
 */

#include <stddef.h>
#include "virtio_mmio.h"
#include "virtio_net_mmio.h"
#include "virtio/virtio_irq.h"
#include "include/config/virtio_net.h"
#include "include/config/virtio_config.h"
#include "include/helper.h"
#include "../virq.h"
#include "../util/util.h"
#include "../libsharedringbuffer/include/shared_ringbuffer.h"

// @jade: add some giant comments about this file

#define RX_QUEUE 0
#define TX_QUEUE 1

// @jade, @ericc: These are sDDF specific, belong in a configuration file elsewhere ideally
#define SHMEM_NUM_BUFFERS 512
#define SHMEM_BUF_SIZE 2048

// @jade, @ivanv: need to be able to get it from vgic
#define VCPU_ID 0

// @jade: random number that I picked, maybe a smaller buffer size would be better?
#define TMP_BUF_SIZE 2048

// mmio handler of this instance of virtio net layer
static virtio_mmio_handler_t net_mmio_handler;

// the list of virtqueue handlers for this instance of virtio net layer
static virtqueue_t vqs[VIRTIO_NET_MMIO_NUM_VIRTQUEUE];

// temporary buffer to transmit buffer from this layer to the backend layer
static char temp_buf[TMP_BUF_SIZE];

// @jade: find a nice place
#define NET_CLIENT_TX_CH        2
#define NET_CLIENT_GET_MAC_CH   4

ring_handle_t net_client_rx_ring;
ring_handle_t net_client_tx_ring;

typedef struct ethernet_buffer {
    /* The acutal underlying memory of the buffer */
    uintptr_t buffer;
    /* The physical size of the buffer */
    size_t size;
    /* Queue from which the buffer was allocated */
    char origin;
    /* Index into buffer_metadata array */
    unsigned int index;
    /* in use */
    bool in_use;
} ethernet_buffer_t;

ethernet_buffer_t buffer_metadata[SHMEM_NUM_BUFFERS * 2];

static bool virtio_net_mmio_handle_rx(void *buf, uint32_t size);

// static void dump_payload(int len, uint8_t *buffer)
// {
//     printf("-------------------- payload start --------------------\n");
//     for (int i = 0; i < len; i++) {
//         printf("%02x ", buffer[i]);
//     }
//     printf("\n");
//     printf("--------------------- payload end ---------------------\n");
//     printf("\n\n");
// }

static void net_client_get_mac(uint8_t *retval)
{
    microkit_ppcall(NET_CLIENT_GET_MAC_CH, microkit_msginfo_new(0, 0));
    uint32_t palr = microkit_mr_get(0);
    uint32_t paur = microkit_mr_get(1);

    retval[5] = paur >> 8 & 0xff;
    retval[4] = paur & 0xff;
    retval[3] = palr >> 24;
    retval[2] = palr >> 16 & 0xff;
    retval[1] = palr >> 8 & 0xff;
    retval[0] = palr & 0xff;
    printf("\"%s\"|VMM NET CLIENT|INFO: net_client_get_mac\n", microkit_name);
}

// sent packet from this vmm to another
static bool net_client_tx(void *buf, uint32_t size)
{
    uintptr_t addr;
    unsigned int len;
    void *cookie;

    // get a buffer from the avail ring
    int error = dequeue_avail(&net_client_tx_ring, &addr, &len, &cookie);
    if (error) {
        printf("\"%s\"|VMM NET CLIENT|INFO: avail ring is empty\n", microkit_name);
        return false;
    }
    assert(size <= len);

    // @jade: eliminate this copy
    memcpy((void *)addr, buf, size);

    struct ether_addr *macaddr = (struct ether_addr *)addr;
    if (macaddr->etype[0] & 0x8) {
        printf("\"%s\"|VIRTIO MMIO|INFO: outgoing, dest MAC: "PR_MAC802_ADDR", src MAC: "PR_MAC802_ADDR", type: 0x%02x%02x\n",
                microkit_name, PR_MAC802_DEST_ADDR_ARGS(macaddr), PR_MAC802_SRC_ADDR_ARGS(macaddr),
                macaddr->etype[0], macaddr->etype[1]);
        // dump_payload(size - 14, macaddr->payload);
    }
    /* insert into the used ring */
    error = enqueue_used(&net_client_tx_ring, addr, size, NULL);
    if (error) {
        printf("\"%s\"|VMM NET CLIENT|INFO: TX used ring full\n", microkit_name);
        enqueue_avail(&net_client_tx_ring, addr, len, NULL);
        return false;
    }

    /* notify the other end */
    microkit_notify(NET_CLIENT_TX_CH);

    return true;
}

bool net_client_rx(void)
{
    uintptr_t addr;
    unsigned int len;
    void *cookie;

    while(!ring_empty(net_client_rx_ring.used_ring)) {
        int error = dequeue_used(&net_client_rx_ring, &addr, &len, &cookie);
        // RX used ring is empty, this is not suppose to happend!
        assert(!error);

        // struct ether_addr *macaddr = (struct ether_addr *)addr;
        // if (macaddr->etype[0] & 0x8 && macaddr->etype[1] & 0x6) {
        //     printf("\"%s\"|VIRTIO MMIO|INFO: incoming, dest MAC: "PR_MAC802_ADDR", src MAC: "PR_MAC802_ADDR", type: 0x%02x%02x\n",
        //             microkit_name, PR_MAC802_DEST_ADDR_ARGS(macaddr), PR_MAC802_SRC_ADDR_ARGS(macaddr),
        //             macaddr->etype[0], macaddr->etype[1]);
        //     dump_payload(len - 14, macaddr->payload);
        // }
        // @jade: handle errors
        virtio_net_mmio_handle_rx((void *)addr, len);
        enqueue_avail(&net_client_rx_ring, addr, SHMEM_BUF_SIZE, NULL);
    }

    return true;
}

void virtio_net_mmio_ack(uint64_t vcpu_id, int irq, void *cookie) {
    // printf("\"%s\"|VIRTIO NET|INFO: virtio_net_ack %d\n", microkit_name, irq);
    // nothing needs to be done
}

virtio_mmio_handler_t *get_virtio_net_mmio_handler(void)
{
    // san check in case somebody wants to get the handler of an uninitialised backend
    if (net_mmio_handler.data.VendorID != VIRTIO_MMIO_DEV_VENDOR_ID) {
        return NULL;
    }
    return &net_mmio_handler;
}

void virtio_net_mmio_reset(void)
{
    vqs[RX_QUEUE].ready = 0;
    vqs[RX_QUEUE].last_idx = 0;

    vqs[TX_QUEUE].ready = 0;
    vqs[TX_QUEUE].last_idx = 0;
}

int virtio_net_mmio_get_device_features(uint32_t *features)
{
    if (net_mmio_handler.data.Status & VIRTIO_CONFIG_S_FEATURES_OK) {
        printf("VIRTIO NET|WARNING: driver somehow wants to read device features after FEATURES_OK\n");
    }

    switch (net_mmio_handler.data.DeviceFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            *features = BIT_LOW(VIRTIO_NET_F_MAC);
            break;
        // features bits 32 to 63
        case 1:
            *features = BIT_HIGH(VIRTIO_F_VERSION_1);
            break;
        default:
            printf("VIRTIO NET|INFO: driver sets DeviceFeaturesSel to 0x%x, which doesn't make sense\n", net_mmio_handler.data.DeviceFeaturesSel);
            return 0;
    }
    return 1;
}

int virtio_net_mmio_set_driver_features(uint32_t features)
{
    int success = 1;

    switch (net_mmio_handler.data.DriverFeaturesSel) {
        // feature bits 0 to 31
        case 0:
            // The device initialisation protocol says the driver should read device feature bits,
            // and write the subset of feature bits understood by the OS and driver to the device.
            // Currently we only have one feature to check.
            success = (features == BIT_LOW(VIRTIO_NET_F_MAC));
            break;

        // features bits 32 to 63
        case 1:
            success = (features == BIT_HIGH(VIRTIO_F_VERSION_1));
            break;

        default:
            printf("VIRTIO NET|INFO: driver sets DriverFeaturesSel to 0x%x, which doesn't make sense\n", net_mmio_handler.data.DriverFeaturesSel);
            success = 0;
    }
    if (success) {
        net_mmio_handler.data.features_happy = 1;
    }
    return success;
}

int virtio_net_mmio_get_device_config(uint32_t offset, uint32_t *ret_val)
{
    // @jade: this function might need a refactor when the virtio net backend starts to
    // support more features
    switch (offset) {
        // get mac low
        case REG_RANGE(0x100, 0x104):
        {
            uint8_t mac[6];
            net_client_get_mac(mac);
            *ret_val = mac[0];
            *ret_val |= mac[1] << 8;
            *ret_val |= mac[2] << 16;
            *ret_val |= mac[3] << 24;
            break;
        }
        // get mac high
        case REG_RANGE(0x104, 0x108):
        {
            uint8_t mac[6];
            net_client_get_mac(mac);
            *ret_val = mac[4];
            *ret_val |= mac[5] << 8;
            break;
        }
        default:
            printf("VIRTIO NET|WARNING: unknown device config register: 0x%x\n", offset);
            return 0;
    }
    return 1;
}

int virtio_net_mmio_set_device_config(uint32_t offset, uint32_t val)
{
    printf("VIRTIO NET|WARNING: driver attempts to set device config but virtio net only has driver-read-only configuration fields\n");
    return 0;
}

// notify the guest VM that we successfully delivered their packet
static void virtio_net_mmio_tx_complete(uint16_t desc_head)
{
        // set the reason of the irq
        net_mmio_handler.data.InterruptStatus = BIT_LOW(0);

        bool success = virq_inject(VCPU_ID, VIRTIO_NET_IRQ);
        // we can't inject irqs?? panic.
        assert(success);

        //add to useds
        struct vring *vring = &vqs[TX_QUEUE].vring;

        struct vring_used_elem used_elem = {desc_head, 0};
        uint16_t guest_idx = vring->used->idx;

        vring->used->ring[guest_idx % vring->num] = used_elem;
        vring->used->idx++;
}

// handle queue notify from the guest VM
static int virtio_net_mmio_handle_queue_notify_tx()
{
    struct vring *vring = &vqs[TX_QUEUE].vring;

    /* read the current guest index */
    uint16_t guest_idx = vring->avail->idx;
    uint16_t idx = vqs[TX_QUEUE].last_idx;

    for (; idx != guest_idx; idx++) {
        /* read the head of the descriptor chain */
        uint16_t desc_head = vring->avail->ring[idx % vring->num];

        /* byte written */
        uint32_t written = 0;

        /* we want to skip the initial virtio header, as this should
         * not be sent to the actual ethernet driver. This records
         * how much we have skipped so far. */
        uint32_t skipped = 0;

        uint16_t curr_desc_head = desc_head;

        do {
            uint32_t skipping = 0;
            /* if we haven't yet skipped the full virtio net header, work
             * out how much of this descriptor should be skipped */
            if (skipped < sizeof(struct virtio_net_hdr_mrg_rxbuf)) {
                skipping = MIN(sizeof(struct virtio_net_hdr_mrg_rxbuf) - skipped, vring->desc[curr_desc_head].len);
                skipped += skipping;
            }

            /* truncate packets that are large than BUF_SIZE */
            uint32_t writing = MIN(TMP_BUF_SIZE - written, vring->desc[curr_desc_head].len - skipping);

            // @jade: we want to eliminate this copy
            memcpy(temp_buf + written, (void *)vring->desc[curr_desc_head].addr + skipping, writing);
            written += writing;
            curr_desc_head = vring->desc[curr_desc_head].next;
        } while (vring->desc[curr_desc_head].flags & VRING_DESC_F_NEXT);

        /* ship the buffer to the next layer */
        int success = net_client_tx(temp_buf, written);
        if (!success) {
            printf("VIRTIO NET|WARNING: VirtIO Net failed to deliver packet for the guest\n.");
        }

        virtio_net_mmio_tx_complete(desc_head);
    }

    vqs[TX_QUEUE].last_idx = idx;

    return 1;
}

// handle rx from client
static bool virtio_net_mmio_handle_rx(void *buf, uint32_t size)
{
    if (!vqs[RX_QUEUE].ready) {
        // vq is not initialised, drop the packet
        return false;
    }
    struct vring *vring = &vqs[RX_QUEUE].vring;

    /* grab the next receive chain */
    uint16_t guest_idx = vring->avail->idx;
    uint16_t idx = vqs[RX_QUEUE].last_idx;

    if (idx == guest_idx) {
        // vq is full or not fully initialised (in this case idx and guest_idx are both 0s), drop the packet
        return false;
    }

    struct virtio_net_hdr_mrg_rxbuf virtio_hdr = {0};
    virtio_hdr.num_buffers = 1;
    // memzero(&virtio_hdr, sizeof(virtio_hdr));

    /* total length of the copied packet */
    size_t copied = 0;
    /* amount of the current descriptor copied */
    size_t desc_copied = 0;

    /* read the head of the descriptor chain */
    uint16_t desc_head = vring->avail->ring[idx % vring->num];
    uint16_t curr_desc_head = desc_head;

    // have we finished copying the net header?
    bool net_header_processed = false;

    do {
        /* determine how much we can copy */
        uint32_t copying;
        /* what are we copying? */
        void *buf_base;

        // process the net header
        if (!net_header_processed) {
            copying = sizeof(struct virtio_net_hdr_mrg_rxbuf) - copied;
            buf_base = &virtio_hdr;

        // otherwise, process the actual packet
        } else {
            copying = size - copied;
            buf_base = buf;
        }

        copying = MIN(copying, vring->desc[curr_desc_head].len - desc_copied);

        memcpy((void *)vring->desc[curr_desc_head].addr + desc_copied, buf_base + copied, copying);

        /* update amounts */
        copied += copying;
        desc_copied += copying;

        // do we need another buffer from the virtqueue?
        if (desc_copied == vring->desc[curr_desc_head].len) {
            if (!(vring->desc[curr_desc_head].flags & VRING_DESC_F_NEXT)) {
                /* descriptor chain is too short to hold the whole packet.
                * just truncate */
                break;
            }
            curr_desc_head = vring->desc[curr_desc_head].next;
            desc_copied = 0;
        }

        // have we finished copying the net header?
        if (copied == sizeof(struct virtio_net_hdr_mrg_rxbuf)) {
            copied = 0;
            net_header_processed = true;
        }

    } while (!net_header_processed || copied < size);

    // record the real amount we have copied
    if (net_header_processed) {
        copied += sizeof(struct virtio_net_hdr_mrg_rxbuf);
    }
    /* now put it in the used ring */
    struct vring_used_elem used_elem = {desc_head, copied};
    uint16_t used_idx = vring->used->idx;

    vring->used->ring[used_idx % vring->num] = used_elem;
    vring->used->idx++;

    /* record that we've used this descriptor chain now */
    vqs[RX_QUEUE].last_idx++;

    // set the reason of the irq
    virtio_mmio_handler_t *handler = get_virtio_net_mmio_handler();
    assert(handler);
    handler->data.InterruptStatus = BIT_LOW(0);

    // notify the guest
    bool success = virq_inject(VCPU_ID, VIRTIO_NET_IRQ);
    // we can't inject irqs?? panic.
    assert(success);

    return true;
}

static virtio_mmio_funs_t mmio_funs = {
    .device_reset = virtio_net_mmio_reset,
    .get_device_features = virtio_net_mmio_get_device_features,
    .set_driver_features = virtio_net_mmio_set_driver_features,
    .get_device_config = virtio_net_mmio_get_device_config,
    .set_device_config = virtio_net_mmio_set_device_config,
    .queue_notify = virtio_net_mmio_handle_queue_notify_tx,
};

void virtio_net_mmio_init(uintptr_t net_client_tx_avail, uintptr_t net_client_tx_used, 
                          uintptr_t net_client_rx_avail, uintptr_t net_client_rx_used, uintptr_t net_client_shared_dma_vaddr)
{    
    /* initialize the client interface */
    ring_init(&net_client_rx_ring, (ring_buffer_t *)net_client_rx_avail, (ring_buffer_t *)net_client_rx_used, NULL, 1);
    ring_init(&net_client_tx_ring, (ring_buffer_t *)net_client_tx_avail, (ring_buffer_t *)net_client_tx_used, NULL, 1);

    /* fill RX avail queue with empty buffers */
    for (int i = 0; i < SHMEM_NUM_BUFFERS - 1; i++) {
        ethernet_buffer_t *buffer = &buffer_metadata[i];
        *buffer = (ethernet_buffer_t) {
            .buffer = net_client_shared_dma_vaddr + (SHMEM_BUF_SIZE * i),
            .size = SHMEM_BUF_SIZE,
            .origin = ORIGIN_RX_QUEUE,
            .index = i,
            .in_use = false,
        };
        int ret = enqueue_avail(&net_client_rx_ring, buffer->buffer, SHMEM_BUF_SIZE, buffer);
        assert(ret == 0);
    }

    /* fill TX avail queue with empty buffers */
    for (int i = 0; i < SHMEM_NUM_BUFFERS - 1; i++) {
        ethernet_buffer_t *buffer = &buffer_metadata[i + SHMEM_NUM_BUFFERS];
        *buffer = (ethernet_buffer_t) {
            .buffer = net_client_shared_dma_vaddr + (SHMEM_BUF_SIZE * (i + SHMEM_NUM_BUFFERS)),
            .size = SHMEM_BUF_SIZE,
            .origin = ORIGIN_TX_QUEUE,
            .index = i + SHMEM_NUM_BUFFERS,
            .in_use = false,
        };
        int ret = enqueue_avail(&net_client_tx_ring, buffer->buffer, SHMEM_BUF_SIZE, buffer);
        assert(ret == 0);
    }

    /* initialize the virtio mmio interface */
    net_mmio_handler.data.DeviceID = DEVICE_ID_VIRTIO_NET;
    net_mmio_handler.data.VendorID = VIRTIO_MMIO_DEV_VENDOR_ID;
    net_mmio_handler.funs = &mmio_funs;

    net_mmio_handler.vqs = vqs;

    /* tell the driver to start doing work. This is part of the protocol of odroidc2 driver implementation
     * and doesn't apply to any else.
     * @jade: this needs to be change when we add a mux */
    microkit_notify(NET_CLIENT_GET_MAC_CH);
}
