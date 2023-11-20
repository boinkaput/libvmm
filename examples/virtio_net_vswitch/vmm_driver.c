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
/* Specific to the virtio_mmio example */
#include "virtio/virtio_irq.h"
#include "virtio/virtio_mem.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_net_mmio.h"

#if defined(BOARD_qemu_arm_virt)
#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x47000000
#define GUEST_INIT_RAM_DISK_VADDR 0x46000000
#elif defined(BOARD_odroidc4)
#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
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
/* microkit will set this variable to the start of the guest RAM memory region. */
uintptr_t guest_ram_vaddr;

#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

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

/* sDDF memory regions for virtio net */
uintptr_t net_rx_avail;
uintptr_t net_rx_used;
uintptr_t net_tx_avail;
uintptr_t net_tx_used;
uintptr_t net_shared_dma_vaddr;

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


#if defined(BOARD_qemu_arm_virt)
    register_passthrough_irq(33, 3);
#elif defined(BOARD_odroidc4)
    register_passthrough_irq(225, 3);
    register_passthrough_irq(5, 5);
    register_passthrough_irq(96, 0);
    register_passthrough_irq(40, 1);
#endif

    // Register virtio_mmio faults
    fault_register_vm_exception_handler(VIRTIO_ADDRESS_START, VIRTIO_ADDRESS_END - VIRTIO_ADDRESS_START, &virtio_mmio_handle_fault);

    // Register virtio_net device
    virtio_net_mmio_init(net_tx_avail, net_tx_used, net_tx_shared_dma_vaddr, net_rx_avail, net_rx_used, net_rx_shared_dma_vaddr);
    virq_register(GUEST_VCPU_ID, VIRTIO_NET_IRQ, &virtio_net_mmio_ack, NULL);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
        // case VSWITCH_CONN_CH_1:
        //     vswitch_rx(ch);
        //     break;
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
