use std::time::Instant;

use omnistack_core::prelude::*;

#[derive(Debug)]
struct Node1 {
    last: Instant
}

#[derive(Debug)]
struct Node2;

impl Module for Node1 {
    fn tick(&mut self, ctx: &Context, now: Instant) -> omnistack_core::module_utils::Result<()> {
        if now.duration_since(self.last).as_millis() > 1000 {
            let p = ctx.packet_alloc().unwrap();
            ctx.push_task_downstream(p);
            println!("at {:?}: generated a packet", now);
            self.last = now;
        }

        Ok(())
    }
}

impl Module for Node2 {
    fn process(&mut self, ctx: &Context, packet: *mut Packet) -> omnistack_core::module_utils::Result<()> {
        println!("Node2 received 1 packet");
        ctx.push_task_downstream(packet);

        Ok(())
    }
}

impl Node1 {
    fn new() -> Self {
        Node1 { last: Instant::now() }
    }
}

impl Node2 {
    fn new() -> Self {
        Node2
    }
}

register_module!(Node1, Node1::new);
register_module!(Node2, Node2::new);

fn main() {
    let config = r#"{
        "nodes": [
            { "id": "n1", "name": "Node1" },
            { "id": "n2", "name": "Node2" },
            { "id": "dpdk", "name": "Dpdk" }
        ],
        "edges": [
            ["n1", "n2"],
            ["n2", "dpdk"]
        ]
    }"#;

    Engine::run(config).expect("failed to boot engine");

    // todo: start the omnistack server only once (lock file?)
}
