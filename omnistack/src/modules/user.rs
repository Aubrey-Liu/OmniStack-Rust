use std::time::Instant;

use omnistack_core::{packet::DEFAULT_OFFSET, prelude::*};

#[derive(Debug)]
struct UserNode {
    last: Instant,
}

impl Module for UserNode {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        // pretend we have sent a packet to user

        ctx.packet_dealloc(packet);
        println!("UserNode sent 1 packet to user");

        Ok(())
    }

    fn tick(&mut self, ctx: &Context, now: Instant) -> Result<()> {
        if now.duration_since(self.last).as_millis() > 1 {
            let p = ctx.packet_alloc().unwrap();

            // pretend we received a packet from user
            p.offset = DEFAULT_OFFSET as _;
            p.refcnt = 1;
            p.nic = 0;
            p.length = 1200; // todo

            // println!("UserNode received 1 packet from user");

            ctx.push_task_downstream(p);
            self.last = now;
        }

        Ok(())
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
