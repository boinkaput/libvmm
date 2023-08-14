#![no_std]
#![no_main]
#![feature(never_type)]

use heapless::Deque;

use sel4cp::message::{MessageInfo, NoMessageValue, StatusMessageLabel};
use sel4cp::{memory_region_symbol, protection_domain, Channel, Handler};

use banscii_pl011_driver_interface_types::*;

mod device;

use device::{Pl011Device, Pl011RegisterBlock};

const DEVICE: Channel = Channel::new(0);
const ASSISTANT: Channel = Channel::new(1);

#[protection_domain]
fn init() -> ThisHandler {
    debug_println!("VMM|INFO: starting Rust VMM");
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
