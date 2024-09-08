/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/util/atomic.h>
#include <libvmm/util/util.h>
#include <libvmm/virtio/virtio.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>
#include <libvmm/arch/aarch64/hsr.h>
#include <sddf/serial/queue.h>
#include <serial_config.h>
#include <sddf/sound/queue.h>
#include <sddf/util/cache.h>
#include <uio/sound.h>

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x1400000

#if defined(BOARD_qemu_virt_aarch64)
#define GUEST_DTB_VADDR 0x405ff288
#define GUEST_INIT_RAM_DISK_VADDR 0x40400000
#elif defined(BOARD_odroidc4)
#define GUEST_DTB_VADDR 0x27000000
#define GUEST_INIT_RAM_DISK_VADDR 0x26000000
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

#define SND_CLIENT_CH 4

#define UIO_SND_IRQ 50

#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

#define SERIAL_TX_CH 1
#define SERIAL_RX_CH 2

#define VIRTIO_CONSOLE_IRQ (74)
#define VIRTIO_CONSOLE_BASE (0x130000)
#define VIRTIO_CONSOLE_SIZE (0x1000)

#define KERNEL_IMAGE_SIZE 0x400000
#define PAGE_SIZE 4096
#define PAGE_ALIGN(vaddr) ((vaddr) & ~(PAGE_SIZE - 1))

#define IS_INSTRUCTION_ABORT(fsr) (((fsr) >> 26) == 0b100000)
#define IS_LEVEL3_FAULT(fsr) (((fsr) & 0b11) == 0b11)
#define IS_TRANSLATION_FAULT(fsr) ((((fsr) >> 2) & 0b1) == 0b1)
#define IS_PERMISSION_FAULT(fsr) ((((fsr) >> 2) & 0b11) == 0b11)

/* Microkit will set this variable to the start of the guest RAM memory region. */
uintptr_t driver_vm_kernel_vaddr;

serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;

char *serial_rx_data;
char *serial_tx_data;

uintptr_t sound_shared_state;
uintptr_t sound_data_paddr;

seL4_CPtr driver_vm_kernel_page_cap;
seL4_CPtr snd_driver_vm_ram_page_cap;
seL4_CPtr zero_page_cap;

seL4_CPtr base_free_slot;

static struct virtio_console_device virtio_console;
static bool suspended;

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

static bool uio_sound_fault_handler(size_t vcpu_id,
                                  size_t offset,
                                  size_t fsr,
                                  seL4_UserContext *regs,
                                  void *data) {
    microkit_notify(SND_CLIENT_CH);
    return true;
}

static bool guest_kernel_mem_fault_handler(size_t vcpu_id, size_t offset, size_t fsr,
                                           seL4_UserContext *regs, void *data)
{
    if (!IS_LEVEL3_FAULT(fsr)) {
        LOG_VMM_ERR("invalid page fault level: must be a level 3 fault\n");
        return false;
    }

    if (!IS_TRANSLATION_FAULT(fsr) && !IS_PERMISSION_FAULT(fsr)) {
        LOG_VMM_ERR("invalid page fault kind: must be either a translation or permission fault\n");
        return false;
    }

    seL4_CapRights_t rights;
    seL4_ARM_VMAttributes attr;
    if (IS_INSTRUCTION_ABORT(fsr)) {
        rights = seL4_CanRead;
        attr = seL4_ARM_Default_VMAttributes;
    } else {
        rights = fault_is_write(fsr) ? seL4_ReadWrite : seL4_CanRead;
        attr = seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever;
    }

    seL4_Error err = seL4_ARM_Page_Map(driver_vm_kernel_page_cap + (offset / PAGE_SIZE), VM_VSPACE_CAP,
                                       PAGE_ALIGN(driver_vm_kernel_vaddr + offset), rights, attr);
    if (err != seL4_NoError) {
        LOG_VMM_ERR("failed to map frame to client's vspace: %d\n", err);
        return false;
    }

    // fault_advance_vcpu increments the pc by 4. After handling the fault,
    // we need to re-execute the same instruction.
    regs->pc -= 4;

    return true;
}

static bool guest_mem_fault_handler(size_t vcpu_id, size_t offset, size_t fsr,
                                    seL4_UserContext *regs, void *data)
{
    if (!IS_LEVEL3_FAULT(fsr)) {
        LOG_VMM_ERR("invalid page fault level: must be a level 3 fault\n");
        return false;
    }

    if (!IS_TRANSLATION_FAULT(fsr) && !IS_PERMISSION_FAULT(fsr)) {
        LOG_VMM_ERR("invalid page fault kind: must be either a translation or permission fault\n");
        return false;
    }

    if (!IS_PERMISSION_FAULT(fsr)) {
        num_mappings++;
    }

    seL4_CPtr page_slot;
    seL4_CapRights_t rights;
    seL4_ARM_VMAttributes attr;
    if (IS_INSTRUCTION_ABORT(fsr)) {
        page_slot = snd_driver_vm_ram_page_cap + (offset / PAGE_SIZE);
        rights = seL4_CanRead;
        attr = seL4_ARM_Default_VMAttributes;
    } else {
        if (fault_is_write(fsr)) {
            page_slot = snd_driver_vm_ram_page_cap + (offset / PAGE_SIZE);
            rights = seL4_ReadWrite;
        } else {
            page_slot = base_free_slot + (offset / PAGE_SIZE);
            seL4_Error err = seL4_CNode_Copy(PD_ROOT_CNODE_CAP, page_slot, PD_CNODE_DEPTH,
                                             PD_ROOT_CNODE_CAP, zero_page_cap, PD_CNODE_DEPTH,
                                             rights);
            assert(err == seL4_NoError);
            rights = seL4_CanRead;
        }
        attr = seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever;
    }

    seL4_Error err = seL4_ARM_Page_Map(page_slot, VM_VSPACE_CAP,
                                       PAGE_ALIGN(0x40600000 + offset),
                                       rights, attr);
    if (err != seL4_NoError) {
        LOG_VMM_ERR("failed to map frame to client's vspace: %d\n", err);
        return false;
    }

    // fault_advance_vcpu increments the pc by 4. After handling the fault,
    // we need to re-execute the same instruction.
    regs->pc -= 4;

    return true;
}

static void uio_sound_virq_ack(size_t vcpu_id, int irq, void *cookie) {}

void init(void) {
    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", microkit_name);
    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;

    uintptr_t kernel_pc = linux_setup_images(driver_vm_kernel_vaddr,
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

    assert(serial_rx_data);
    assert(serial_tx_data);
    assert(serial_rx_queue);
    assert(serial_tx_queue);

    /* Initialise our sDDF ring buffers for the serial device */
    serial_queue_handle_t serial_rxq, serial_txq;
    serial_cli_queue_init_sys(microkit_name, &serial_rxq, serial_rx_queue, serial_rx_data, &serial_txq, serial_tx_queue, serial_tx_data);

    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                  VIRTIO_CONSOLE_BASE,
                                  VIRTIO_CONSOLE_SIZE,
                                  VIRTIO_CONSOLE_IRQ,
                                  &serial_rxq, &serial_txq,
                                  SERIAL_TX_CH);
    assert(success);

    success = virq_register(GUEST_VCPU_ID, UIO_SND_IRQ, &uio_sound_virq_ack, NULL);
    assert(success);

    success = fault_register_vm_exception_handler(UIO_SND_FAULT_ADDRESS,
                                                  sizeof(size_t),
                                                  &uio_sound_fault_handler, NULL);
    assert(success);

    success = fault_register_vm_exception_handler(driver_vm_kernel_vaddr, KERNEL_IMAGE_SIZE,
                                                  guest_kernel_mem_fault_handler, NULL);
    assert(success);

    success = fault_register_vm_exception_handler(0x40600000, 0xe00000,
                                                  guest_mem_fault_handler, NULL);
    assert(success);

#if defined(BOARD_qemu_virt_aarch64)
    register_passthrough_irq(37, 5); // Serial
#elif defined(BOARD_odroidc4)
    register_passthrough_irq(48, 5); // USB controller
    register_passthrough_irq(63, 6); // USB 1
    register_passthrough_irq(62, 7); // USB 2
    register_passthrough_irq(5, 8);  // Unknown
#else
#error Need to define passthrough IRQs
#endif

    uintptr_t *data_paddr = &((vm_shared_state_t *)sound_shared_state)->data_paddr;
    *data_paddr = sound_data_paddr;
    cache_clean((uintptr_t)data_paddr, sizeof(uintptr_t));

    suspended = false;

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    bool success;

    if (suspended) {
        microkit_vcpu_resume(GUEST_VCPU_ID);
        suspended = false;
    }

    switch (ch) {
    case SERIAL_RX_CH:
        /* We have received an event from the serial virtualiser, so we
            * call the virtIO console handling */
        virtio_console_handle_rx(&virtio_console);
        break;
    case SND_CLIENT_CH:
        success = virq_inject(GUEST_VCPU_ID, UIO_SND_IRQ);
        if (!success) {
            LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", UIO_SND_IRQ, GUEST_VCPU_ID);
        }
        break;
    default:
        if (passthrough_irq_map[ch]) {
            success = virq_inject(GUEST_VCPU_ID, passthrough_irq_map[ch]);
            if (!success) {
                LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", passthrough_irq_map[ch], GUEST_VCPU_ID);
            }
        } else {
            printf("Unexpected channel, ch: 0x%lx\n", ch);
        }
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    bool success = fault_handle(child, msginfo);
    if (success) {
        sound_shared_state_t *shared_state = (void *)sound_shared_state;
        size_t label = microkit_msginfo_get_label(msginfo);
        if (label == seL4_Fault_VCPUFault && ATOMIC_LOAD(&shared_state->ready, __ATOMIC_ACQUIRE)) {
            uint64_t hsr_ec_class = HSR_EXCEPTION_CLASS(microkit_mr_get(seL4_VCPUFault_HSR));
            if (hsr_ec_class == HSR_WFx_EXCEPTION) {
                microkit_vcpu_stop(GUEST_VCPU_ID);
                suspended = true;
            }
        }

        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    return seL4_False;
}
