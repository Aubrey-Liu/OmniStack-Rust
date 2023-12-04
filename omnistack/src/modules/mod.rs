pub mod dpdk;
pub mod eth;

use std::time::Instant;

use omnistack_core::prelude::*;

#[derive(Debug)]
struct UserNode {
    last: Instant
}

impl Module for UserNode {
    fn tick(&mut self, ctx: &Context, now: Instant) -> Result<()> {
        if now.duration_since(self.last).as_millis() > 1000 {
            let p = ctx.packet_alloc().unwrap();

            ctx.push_task_downstream(p);
            println!("Node1 generated 1 packet");
            self.last = now;
        }

        Ok(())
    }
}

impl UserNode {
    fn new() -> Self {
        UserNode { last: Instant::now() }
    }
}

register_module!(UserNode);
