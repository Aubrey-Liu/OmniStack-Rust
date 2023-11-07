use omnistack_core::prelude::*;

struct Node1;
struct Node2;

impl Module for Node1 {
    unsafe fn process(&self, packet: *mut Packet) {
        println!("Node1 received packet no.{}", packet.as_ref().unwrap().id);
    }
}

impl Module for Node2 {
    unsafe fn process(&self, packet: *mut Packet) {
        println!("Node2 received packet no.{}", packet.as_ref().unwrap().id);
    }
}

impl Node1 {
    fn new() -> ModuleTy {
        Box::new(Node1)
    }
}

impl Node2 {
    fn new() -> ModuleTy {
        Box::new(Node2)
    }
}

register_module!(Node1, Node1::new);
register_module!(Node2, Node2::new);

fn main() {
    let config = r#"{
        "nodes": [
            { "id": 0, "name": "Node1" },
            { "id": 1, "name": "Node2" }
        ],
        "edges": [
            [0, 1]
        ]
    }"#;

    Engine::init(config);
    Engine::run();
}
