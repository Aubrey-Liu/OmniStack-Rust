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
        if now.duration_since(self.last).as_millis() > 50 {
            let p = ctx.allocate().unwrap();

            // pretend we have received a packet from user
            p.buf[140..1340].fill(3);
            p.offset = DEFAULT_OFFSET as _;
            p.data = p.buf.as_mut_ptr();
            p.refcnt = 1;
            p.port = 0;
            p.set_len(1200);

            log::debug!("Received 1 Packet (len: 1200) from User.");

            ctx.dispatch_task(p);
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
