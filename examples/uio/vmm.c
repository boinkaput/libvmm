/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <sel4cp.h>
#include "util/util.h"
#include "arch/aarch64/vgic/vgic.h"
#include "arch/aarch64/linux.h"
#include "arch/aarch64/fault.h"
#include "guest.h"
#include "virq.h"
#include "tcb.h"
#include "vcpu.h"

// @ivanv: ideally we would have none of these hardcoded values
// initrd, ram size come from the DTB
// We can probably add a node for the DTB addr and then use that.
// Part of the problem is that we might need multiple DTBs for the same example
// e.g one DTB for VMM one, one DTB for VMM two. we should be able to hide all
// of this in the build system to avoid doing any run-time DTB stuff.

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_arm_virt_hyp)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#elif defined(BOARD_rpi4b_hyp)
#define GUEST_DTB_VADDR 0x2e000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc2_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc4_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_imx8mm_evk_hyp)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#else
#error Need to define VM image address and DTB address
#endif

/* For simplicity we just enforce the serial IRQ channel number to be the same
 * across platforms. */
#define SERIAL_IRQ_CH 1

#if defined(BOARD_qemu_arm_virt_hyp)
#define SERIAL_IRQ 33
#define ETH_IRQ 79
#elif defined(BOARD_odroidc2_hyp) || defined(BOARD_odroidc4_hyp)
#define SERIAL_IRQ 225
#define ETH_IRQ 40
#elif defined(BOARD_rpi4b_hyp)
#define SERIAL_IRQ 57
#elif defined(BOARD_imx8mm_evk_hyp)
#define SERIAL_IRQ 79
#else
#error Need to define serial interrupt
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
/* seL4CP will set this variable to the start of the guest RAM memory region. */
uintptr_t guest_ram_vaddr;

/* @tim: Does the VMM actually need these addresses? */
uintptr_t uio_map0;
uintptr_t uio_map1;
uintptr_t uio_map2;

/*
 * seL4CP dictates that there is a maximum of 63 channels (e.g IRQs) that can
 * be associated to a single PD, in this case the VMM.
 */
#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

/* @tim: Do we (or should we) be using these? */ 
#define UIO_SIZE        0x1000
#define UIO_0_START     0xffe50000

static void uio_ack(uint64_t vcpu_id, int irq, void *cookie) {
    printf("uio_ack!!!!!!!!!!\n");
}

static void passthrough_device_ack(uint64_t vcpu_id, int irq, void *cookie) {
    sel4cp_channel irq_ch = (sel4cp_channel)(int64_t)cookie;
    sel4cp_irq_ack(irq_ch);
}

static void register_passthrough_irq(int irq, sel4cp_channel irq_ch) {
    LOG_VMM("Register passthrough IRQ %d (channel: 0x%lx)\n", irq, irq_ch);
    assert(irq_ch < MAX_IRQ_CH);
    passthrough_irq_map[irq_ch] = irq;

    int err = vgic_register_irq(GUEST_VCPU_ID, irq, &passthrough_device_ack, (void *)(int64_t)irq_ch);
    if (!err) {
        LOG_VMM_ERR("Failed to register IRQ %d\n", irq);
        return;
    }
}

static void serial_ack(size_t vcpu_id, int irq, void *cookie) {
    /*
     * For now we by default simply ack the serial IRQ, we have not
     * come across a case yet where more than this needs to be done.
     */
    sel4cp_irq_ack(SERIAL_IRQ_CH);
}

void init(void) {
    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", sel4cp_name);
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

#if defined(DRIVER_GUEST_VMM)
    register_passthrough_irq(SERIAL_IRQ, 1);
    // @tim: ETH_IRQ ID was 5, will need to update in odroid system file
    register_passthrough_irq(ETH_IRQ, 2);

    // register_passthrough_irq(225, 1);
    // register_passthrough_irq(222, 2);
    // register_passthrough_irq(223, 3);
    // register_passthrough_irq(232, 4);

    // register_passthrough_irq(40, 5);
    // register_passthrough_irq(35, 15);

    // register_passthrough_irq(96, 6);
    // register_passthrough_irq(192, 7);
    // register_passthrough_irq(193, 8);
    // register_passthrough_irq(194, 9);
    // register_passthrough_irq(53, 10);
    // register_passthrough_irq(228, 11);
    // register_passthrough_irq(63, 12);
    // register_passthrough_irq(62, 13);
    // register_passthrough_irq(48, 16);
    // register_passthrough_irq(89, 14);
    // // @jade: this should not be necessary. Investigation required.
    // register_passthrough_irq(5, 17);

    // @tim: 42 comes from DTS
    vgic_register_irq(GUEST_VCPU_ID, 42, &uio_ack, NULL);
#endif

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(sel4cp_channel ch) {
    if (passthrough_irq_map[ch]) {
        bool success = vgic_inject_irq(GUEST_VCPU_ID, passthrough_irq_map[ch]);
        if (!success) {
            LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", passthrough_irq_map[ch], GUEST_VCPU_ID);
        }
    } else {
        printf("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler,
 * whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
void fault(sel4cp_id id, sel4cp_msginfo msginfo) {
    if (id != GUEST_VCPU_ID) {
        LOG_VMM_ERR("Unexpected fault from entity with ID 0x%lx\n", id);
        return;
    }

    size_t label = sel4cp_msginfo_get_label(msginfo);
    bool success = false;
    switch (label) {
        case seL4_Fault_VMFault:
            success = fault_handle_vm_exception(id);
            break;
        case seL4_Fault_UnknownSyscall:
            success = fault_handle_unknown_syscall(id);
            break;
        case seL4_Fault_UserException:
            success = fault_handle_user_exception(id);
            break;
        case seL4_Fault_VGICMaintenance:
            success = fault_handle_vgic_maintenance(id);
            break;
        case seL4_Fault_VCPUFault:
            success = fault_handle_vcpu_exception(id);
            break;
        case seL4_Fault_VPPIEvent:
            success = fault_handle_vppi_event(id);
            break;
        default:
            /* We have reached a genuinely unexpected case, stop the guest. */
            LOG_VMM_ERR("unknown fault label 0x%lx, stopping guest with ID 0x%lx\n", label, id);
            sel4cp_vm_stop(id);
            /* Dump the TCB and vCPU registers to hopefully get information as
             * to what has gone wrong. */
            tcb_print_regs(id);
            vcpu_print_regs(id);
            return;
    }

    if (success) {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        sel4cp_fault_reply(sel4cp_msginfo_new(0, 0));
    } else {
        LOG_VMM_ERR("Failed to handle %s fault\n", fault_to_string(label));
    }
}
