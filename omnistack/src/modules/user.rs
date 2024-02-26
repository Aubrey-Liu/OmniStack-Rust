use std::time::Instant;

use omnistack_core::{packet::DEFAULT_OFFSET, prelude::*};

#[derive(Debug)]
struct UserNode {
    last: Instant,
}

impl Module for UserNode {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        // pretend we have sent a packet to user
        ctx.deallocate(packet);

        Ok(())
    }

    fn tick(&mut self, ctx: &Context, now: Instant) -> Result<()> {
        if now.duration_since(self.last).as_millis() > 1000 {
            let p = ctx.allocate_pkt().unwrap();
            let mbuf = ctx.allocate_mbuf().unwrap();
            p.init_from_mbuf(omnistack_core::packet::Mbuf(mbuf));

            // pretend we received a packet from user
            p.offset = DEFAULT_OFFSET as _;
            p.refcnt = 1;
            p.port = 0;
            p.set_len(1200);

            ctx.push_task_downstream(p);
            self.last = now;
        }

        Ok(())
    }

    fn tickable(&self) -> bool {
        true
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
