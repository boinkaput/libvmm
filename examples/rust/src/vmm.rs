#![no_std]
#![no_main]
#![feature(never_type)]

use core::{include_bytes};

use sel4cp::message::{MessageInfo, NoMessageValue, StatusMessageLabel};
use sel4cp::{memory_region_symbol, protection_domain, Channel, Handler, debug_println};

const guest_ram_vaddr: usize = 0x40000000;
const GUEST_DTB_VADDR: usize = 0x4f000000;
const GUEST_INIT_RAM_DISK_VADDR: usize = 0x4d700000;

#[link(name = "vmm", kind = "static")]
#[link(name = "sel4cp", kind = "static")]
extern "C" {
    fn linux_setup_images(ram_start: usize, kernel: usize, kernel_size: usize, dtb_src: usize, dtb_dest: usize, dtb_size: usize, initrd_src: usize, initrd_dest: usize, initrd_size: usize) -> usize;
    fn virq_controller_init(boot_vpcu_id: usize) -> bool;
}

#[protection_domain]
fn init() -> ThisHandler {
    let linux = include_bytes!("../images/linux");
    let dtb = include_bytes!("../build/linux.dtb");
    let initrd = include_bytes!("../images/rootfs.cpio.gz");

    debug_println!("VMM|INFO: starting Rust VMM");

    debug_println!("linux size: {}", linux.len());

    let linux_addr = linux.as_ptr() as usize;
    let dtb_addr = dtb.as_ptr() as usize;
    let initrd_addr = initrd.as_ptr() as usize;

    unsafe {
        let pc = linux_setup_images(guest_ram_vaddr,
                                    linux_addr, linux.len(),
                                    dtb_addr, GUEST_DTB_VADDR, dtb.len(),
                                    initrd_addr, GUEST_INIT_RAM_DISK_VADDR, initrd.len()
                                    );
    }

    ThisHandler {}
}

struct ThisHandler {
}

impl Handler for ThisHandler {
    type Error = !;

    fn notified(&mut self, channel: Channel) -> Result<(), Self::Error> {
        Ok(())
    }
}
