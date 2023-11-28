use crate::packet::Packet;
use crate::register_module;

use crate::module_utils::*;

#[repr(C)]
#[derive(Debug)]
pub struct Dpdk {}

impl Dpdk {
    pub fn new() -> Self {
        Self {}
    }
}

unsafe impl Send for Dpdk {}

impl Module for Dpdk {
    fn init(&mut self) -> Result<()> {
        Ok(())
    }

    fn process(&mut self, ctx: &crate::prelude::Context, packet: *mut Packet) -> Result<()> {
        ctx.packet_dealloc(packet);
        println!("deallocated 1 packet");

        Ok(())
    }

    fn tick(&mut self, _ctx: &crate::prelude::Context, _now: std::time::Instant) -> Result<()> {
        Ok(())
    }
}

register_module!(Dpdk, Dpdk::new);
// module_cbindgen!(Dpdk, dpdk);
