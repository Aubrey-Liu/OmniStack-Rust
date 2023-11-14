use omnistack_core::prelude::*;

struct Node1;
struct Node2;

impl Module for Node1 {
    fn tick(&self, ctx: &Context, now: std::time::Instant) -> omnistack_core::modules::Result<()> {
        let p = PacketPool::allocate().unwrap();
        ctx.generate_downsteam_tasks(p);

        println!("at {:?}: generated a packet", now);

        Ok(())
    }
}

impl Module for Node2 {
    fn process(&self, ctx: &Context, packet: *mut Packet) -> omnistack_core::modules::Result<()> {
        println!("Node2 received 1 packet");
        ctx.generate_downsteam_tasks(packet);

        Ok(())
    }
}

impl Node1 {
    fn new() -> Self {
        Node1
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
            { "id": "nic", "name": "Nic" },
            { "id": "n2", "name": "Node2" }
        ],
        "edges": [
            ["n1", "nic"],
            ["nic", "n1"],
            ["n1", "n2"],
            ["n2", "nic"]
        ]
    }"#;

    Engine::run(config).expect("failed to boot engine");
}
