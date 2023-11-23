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

unsafe impl Send for Dpdk {}

impl Module for Dpdk {
    fn init(&mut self) -> Result<()> {
        // let ret = unsafe { ffi::dpdk_init(self as *mut _) };

        // if ret == 0 {
        //     Ok(())
        // } else {
        //     Err(Error::Unknown)
        // }
        Ok(())
    }

    fn process(&mut self, ctx: &crate::prelude::Context, packet: *mut Packet) -> Result<()> {
        // let ret = unsafe { ffi::dpdk_process(self as *mut _, ctx, packet) };

        // if ret == 0 {
        //     Ok(())
        // } else {
        //     Err(Error::Unknown)
        // }

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
