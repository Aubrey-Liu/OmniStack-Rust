#![allow(unused)]
use std::ptr::null_mut;
use std::time::Instant;

use omnistack_core::module::ModuleCapa;
use omnistack_core::packet::{PktBufType, DEFAULT_OFFSET};
use omnistack_core::prelude::*;

const BURST_SIZE: usize = 32;

#[derive(Debug)]
struct UserNode {
    rx_queue: [*mut Packet; BURST_SIZE],
}

impl Module for UserNode {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        PacketPool::deallocate(packet);

        // pretend we have sent a packet to user
        Err(ModuleError::Done)
    }

    #[cfg(feature = "send")]
    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        for i in 0..BURST_SIZE {
            self.rx_queue[i] = ctx.allocate().unwrap();
        }

        let mut ret = null_mut();
        for i in (0..BURST_SIZE).rev() {
            let pkt = unsafe { &mut *self.rx_queue[i] };
            // let size = 1500 - 28;
            let size = 64 - 28;

            // pretend we have received a packet from user
            pkt.data = pkt.buf.as_mut_ptr();
            pkt.buf_ty = PktBufType::Local;
            pkt.offset = DEFAULT_OFFSET as _;
            pkt.set_len(size as _);
            // TODO: read from user config
            pkt.nic = 0;
            pkt.refcnt = 1;
            pkt.next = ret;
            ret = pkt;
        }

        Ok(unsafe { &mut *ret })
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }
}

impl UserNode {
    fn new() -> Self {
        UserNode {
            rx_queue: [null_mut(); BURST_SIZE],
        }
    }
}

register_module!(UserNode);
