use std::ptr::null_mut;

use crate::engine::*;
use crate::module::*;
use crate::packet::*;
use crate::prelude::Error;
use crate::Result;

pub const BURST_SIZE: usize = 32;

#[derive(omnistack_proc::Module)]
#[repr(align(64))]
struct UserNode {
    rx_queue: [*mut Packet; BURST_SIZE],
}

impl UserNode {
    fn new() -> Self {
        UserNode {
            rx_queue: [null_mut(); BURST_SIZE],
        }
    }
}

impl Module for UserNode {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        // TODO: deliver the packet to user
        PacketPool::deallocate(packet);

        // NOTE: send packet to user, but re-collect mbufs on the server side
        // to ensure efficient cache utilization

        Err(Error::Dropped)
    }

    // #[cfg(not(feature = "rxonly"))]
    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        // TODO: receive from users

        ctx.pktpool
            .allocate_many(BURST_SIZE as _, &mut self.rx_queue)?;

        let mut ret = null_mut();
        for &pkt in self.rx_queue.iter().rev() {
            let pkt = unsafe { &mut *pkt };
            // let size = 1500 - 28;
            let size = 64 - 28;

            pkt.data = pkt.buf.0.as_mut_ptr();
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

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }
}
