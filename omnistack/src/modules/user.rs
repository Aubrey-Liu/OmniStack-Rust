#![allow(unused)]
use std::ptr::null_mut;

use omnistack_core::module::ModuleCapa;
use omnistack_core::packet::{PktBufType, DEFAULT_OFFSET};
use omnistack_core::prelude::*;

const BATCH_SIZE: usize = 32;

struct UserNode {
    rx_queue: [*mut Packet; BATCH_SIZE],
}

impl Module for UserNode {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        PacketPool::deallocate(packet);

        // pretend we have sent a packet to user
        Err(ModuleError::Done)
    }

    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        let ret = ctx
            .pktpool
            .allocate_many(BATCH_SIZE as _, &mut self.rx_queue);
        if ret != 0 {
            return Err(ModuleError::OutofMemory);
        }

        let mut ret = null_mut();
        for &pkt in self.rx_queue.iter().rev() {
            let pkt = unsafe { &mut *pkt };
            // let size = 1500 - 28;
            let size = 64 - 28;

            pkt.data = pkt.buf.as_mut_ptr();
            pkt.buf_ty = PktBufType::Local;
            pkt.offset = DEFAULT_OFFSET as _;
            pkt.set_len(size as _);
            pkt.nic = 0; // TODO: read from user config
            pkt.refcnt = 1;
            pkt.next = ret;
            ret = pkt;
        }

        Ok(unsafe { &mut *ret })
    }

    #[cfg(not(feature = "rxonly"))]
    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }
}

impl UserNode {
    fn new() -> Self {
        UserNode {
            rx_queue: [null_mut(); BATCH_SIZE],
        }
    }
}

register_module!(UserNode);
