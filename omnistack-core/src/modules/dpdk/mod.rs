use crate::module_cbindgen;
use crate::packet::Packet;
use crate::register_module;

use crate::module_utils::*;

#[repr(C)]
pub struct Dpdk {}

impl Dpdk {
    pub fn new() -> Self {
        Self {}
    }
}

impl Module for Dpdk {
    fn process(&mut self, ctx: &crate::prelude::Context, packet: *mut Packet) -> Result<()> {
        let cctx = ctx.into();
        let _ret = unsafe { ffi::dpdk_process(self as *mut _, &cctx, packet) };

        Ok(())
    }

    fn tick(&mut self, _ctx: &crate::prelude::Context, _now: std::time::Instant) -> Result<()> {
        Ok(())
    }
}

register_module!(Dpdk, Dpdk::new);
module_cbindgen!(Dpdk, dpdk);
