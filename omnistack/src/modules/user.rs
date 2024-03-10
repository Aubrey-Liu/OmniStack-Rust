use std::time::Instant;

use omnistack_core::module::ModuleCapa;
use omnistack_core::packet::{PktBufType, DEFAULT_OFFSET};
use omnistack_core::prelude::*;

#[derive(Debug)]
struct UserNode {
    last: Instant,
}

impl Module for UserNode {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        // pretend we have sent a packet to user
        ctx.deallocate(packet);

        Err(ModuleError::Dropped)
    }

    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        let now = Instant::now();
        if true {
            let pkt = ctx.allocate().unwrap();

            // pretend we have received a packet from user
            pkt.buf[140..1340].fill(3);
            pkt.offset = DEFAULT_OFFSET as _;
            pkt.data = pkt.buf.as_mut_ptr();
            pkt.refcnt = 1;
            pkt.nic = 0;
            pkt.set_len(1200);
            pkt.buf_ty = PktBufType::Local;

            // log::debug!(
            //     "Core[{}] - Received 1 Packet (len: 1200) from user",
            //     ctx.cpu
            // );

            self.last = now;

            Ok(pkt)
        } else {
            Err(ModuleError::NoData)
        }
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }
}

impl UserNode {
    fn new() -> Self {
        UserNode {
            last: Instant::now(),
        }
    }
}

register_module!(UserNode);
