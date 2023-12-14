/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include "util/util.h"
#include "arch/aarch64/vgic/vgic.h"
#include "arch/aarch64/linux.h"
#include "arch/aarch64/fault.h"
#include "guest.h"
#include "virq.h"
#include "tcb.h"
#include "vcpu.h"
#include "virtio/console.h"

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_arm_virt)
#define GUEST_DTB_VADDR 0x47000000
#define GUEST_INIT_RAM_DISK_VADDR 0x46000000
#elif defined(BOARD_odroidc4)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#else
#error Need to define guest kernel image address and DTB address
#endif

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];
/* Microkit will set this variable to the start of the guest RAM memory region. */
uintptr_t guest_ram_vaddr;

uintptr_t serial_rx_free;
uintptr_t serial_rx_used;
uintptr_t serial_tx_free;
uintptr_t serial_tx_used;

uintptr_t serial_rx_data;
uintptr_t serial_tx_data;

// @jade: how do I tell the userlevel program about these?
uintptr_t net_driver_rx_free;
uintptr_t net_driver_rx_used;
uintptr_t net_driver_tx_free;
uintptr_t net_driver_tx_used;
uintptr_t net_driver_shared_dma_vaddr;
uintptr_t net_driver_shared_dma_paddr;

#define SERIAL_MUX_TX_CH 1
#define SERIAL_MUX_RX_CH 2

#define NET_MUX_RX_CH 3
#define NET_MUX_TX_CH 4
#define NET_MUX_GET_MAC_CH 5

#define VIRTIO_CONSOLE_IRQ (74)
#define VIRTIO_CONSOLE_BASE (0x130000)
#define VIRTIO_CONSOLE_SIZE (0x1000)

#define UIO_IRQ (73)

static struct virtio_device virtio_console;

#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

static void uio_ack(uint64_t vcpu_id, int irq, void *cookie) {
    printf("uio_ack!!!!!!!!!!\n");
}

static void passthrough_device_ack(size_t vcpu_id, int irq, void *cookie) {
    microkit_channel irq_ch = (microkit_channel)(int64_t)cookie;
    microkit_irq_ack(irq_ch);
}

static void register_passthrough_irq(int irq, microkit_channel irq_ch) {
    LOG_VMM("Register passthrough IRQ %d (channel: 0x%lx)\n", irq, irq_ch);
    assert(irq_ch < MAX_IRQ_CH);
    passthrough_irq_map[irq_ch] = irq;

    int err = virq_register(GUEST_VCPU_ID, irq, &passthrough_device_ack, (void *)(int64_t)irq_ch);
    if (!err) {
        LOG_VMM_ERR("Failed to register IRQ %d\n", irq);
        return;
    }
}

void init(void) {
    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", microkit_name);
    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    uintptr_t kernel_pc = linux_setup_images(guest_ram_vaddr,
                                      (uintptr_t) _guest_kernel_image,
                                      kernel_size,
                                      (uintptr_t) _guest_dtb_image,
                                      GUEST_DTB_VADDR,
                                      dtb_size,
                                      (uintptr_t) _guest_initrd_image,
                                      GUEST_INIT_RAM_DISK_VADDR,
                                      initrd_size
                                      );
    if (!kernel_pc) {
        LOG_VMM_ERR("Failed to initialise guest images\n");
        return;
    }
    /* Initialise the virtual GIC driver */
    bool success = virq_controller_init(GUEST_VCPU_ID);
    if (!success) {
        LOG_VMM_ERR("Failed to initialise emulated interrupt controller\n");
        return;
    }

    // register passthrough IRQs
#if defined(BOARD_qemu_arm_virt)
    // nothing to be done
#elif defined(BOARD_odroidc4)
    register_passthrough_irq(96, 12);
    register_passthrough_irq(40, 13);
    register_passthrough_irq(223, 14);
    register_passthrough_irq(5, 15);
#endif

    // virtio console
    virtio_console_init(&virtio_console, 
                        serial_rx_free, serial_rx_used, serial_tx_free, serial_tx_used,
                        serial_rx_data, serial_tx_data,
                        VIRTIO_CONSOLE_IRQ, SERIAL_MUX_TX_CH);
    
    success = fault_register_vm_exception_handler(VIRTIO_CONSOLE_BASE, VIRTIO_CONSOLE_SIZE,
                                                    &virtio_mmio_fault_handle, &virtio_console);
    if (!success) {
        LOG_VMM_ERR("Could not register virtual memory fault handler for "
                    "virtIO region [0x%lx..0x%lx)\n", VIRTIO_CONSOLE_BASE, VIRTIO_CONSOLE_BASE + VIRTIO_CONSOLE_SIZE);
        return;
    }

    /* Register the virtual IRQ that will be used to communicate from the device
     * to the guest. This assumes that the interrupt controller is already setup. */
    // @ivanv: we should check that (on AArch64) the virq is an SPI.
    success = virq_register(GUEST_VCPU_ID, VIRTIO_CONSOLE_IRQ, &virtio_virq_default_ack, NULL);
    assert(success);

    int err = virq_register(GUEST_VCPU_ID, UIO_IRQ, &uio_ack, NULL);
    if (!err) {
        LOG_VMM_ERR("Failed to register IRQ %d\n", UIO_IRQ);
        return;
    }

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

seL4_MessageInfo_t
protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (ch) {
        case NET_MUX_GET_MAC_CH:
            // return the MAC address. 
            //odroidc4-2: 00:1E:06:4A:35:E4
            // FIXME(@jade): absolutely hacky.
            uint32_t mac_l = 0x4a061e00;
            uint32_t mac_h = 0xe435;
            microkit_mr_set(0, mac_l);
            microkit_mr_set(1, mac_h);
            return microkit_msginfo_new(0, 2);
            
        default:
            // microkit_dbg_puts("Received ppc on unexpected channel ");
            // puthex64(ch);
            break;
    }
    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch) {
    switch (ch) {
        case SERIAL_MUX_RX_CH: {
            /* We have received an event from the serial multipelxor, so we
             * call the virtIO console handling */
            virtio_console_handle_rx(&virtio_console);
            break;
        }
        case NET_MUX_RX_CH:
        case NET_MUX_GET_MAC_CH: {
                bool success = virq_inject(GUEST_VCPU_ID, UIO_IRQ);
                if (!success) {
                    LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", UIO_IRQ, GUEST_VCPU_ID);
                }
            break;
        }
        default:
            if (passthrough_irq_map[ch]) {
                bool success = virq_inject(GUEST_VCPU_ID, passthrough_irq_map[ch]);
                if (!success) {
                    LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", passthrough_irq_map[ch], GUEST_VCPU_ID);
                }
                break;
            }
            printf("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler,
 * whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
void fault(microkit_id id, microkit_msginfo msginfo) {
    bool success = fault_handle(id, msginfo);
    if (success) {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        microkit_fault_reply(microkit_msginfo_new(0, 0));
    }
}
