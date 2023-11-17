use std::ffi::c_void;

use crate::module_cbindgen;
use crate::packet::Packet;
use crate::register_module;

use crate::module_utils::*;

#[repr(C)]
pub struct Dpdk {
    mbuf_pool: *mut c_void,
}

impl Dpdk {
    pub fn new() -> Self {
        Self {
            mbuf_pool: std::ptr::null_mut(),
        }
    }
}

unsafe impl Send for Dpdk {}

impl Module for Dpdk {
    fn init(&mut self) -> Result<()> {
        let ret = unsafe { ffi::dpdk_init(self as *mut _) };
        if ret == 0 {
            Ok(())
        } else {
            Err(Error::Unknown)
        }
    }

    fn process(&mut self, ctx: &crate::prelude::Context, packet: *mut Packet) -> Result<()> {
        let cctx = ctx.into();
        let ret = unsafe { ffi::dpdk_process(self as *mut _, &cctx, packet) };

        if ret == 0 {
            Ok(())
        } else {
            Err(Error::Unknown)
        }
    }

    fn tick(&mut self, _ctx: &crate::prelude::Context, _now: std::time::Instant) -> Result<()> {
        Ok(())
    }
}

register_module!(Dpdk, Dpdk::new);
module_cbindgen!(Dpdk, dpdk);
