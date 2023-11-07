#![allow(unused)]

use std::collections::HashMap;
use std::thread;

use static_init::dynamic;
use crossbeam::channel::{Sender, Receiver, bounded, unbounded};
use crate::modules::factory::{Packet, PacketId, MODULE_FACTORY};
use crate::prelude::*;

type NodeId = usize;
type GraphId = usize;

enum LinkType {
    Local,
    Remote(Sender<Message>),
}

struct Link {
    ty: LinkType,
    to: NodeId,
}

impl PartialEq for Link {
    fn eq(&self, other: &Self) -> bool {
        self.to == other.to
    }
}

#[derive(Default)]
pub struct Engine {
    all_nodes: HashMap<NodeId, Node>,
}

#[dynamic]
static mut ENGINE: Engine = Engine::default();

#[dynamic]
static PACKET_POOL: [Packet; 1024] = std::array::from_fn(|_| Packet { id: 0 });

impl Engine {
    pub fn init(config: &str) {
        let config = json::parse(config).expect("config should be in json format");

        let nodes = &config["nodes"];
        let edges = &config["edges"];

        assert!(!nodes.is_null(), "key `nodes` not found in config");
        assert!(!edges.is_null(), "key `edges` not found in config");

        for node in nodes.members() {
            assert!(!node["id"].is_null(), "node doesn't have key `id`");
            assert!(!node["name"].is_null(), "node doesn't have key `name`");

            let id = node["id"]
                .as_usize()
                .expect(r#"invalie node["id"] type, shoule be usize"#);
            let name = node["name"]
                .as_str()
                .expect(r#"invalie node["name"] type, shoule be str"#);

            assert!(
                !ENGINE.read().all_nodes.contains_key(&id),
                "duplicate node id `{}`",
                id
            );
            assert!(
                MODULE_FACTORY.read().contains_key(&name),
                "module name `{}` doesn't exist",
                name
            );

            let new_node = Node::new(MODULE_FACTORY.read().get(&name).unwrap()());
            ENGINE.write().all_nodes.insert(id, new_node);
        }

        for edge in edges.members() {
            assert!(edge.is_array() && edge.members().len() == 2);
            let mut edge = edge.members();
            let (src, dst) = (
                edge.next()
                    .unwrap()
                    .as_usize()
                    .expect("invalid node id type, should be usize"),
                edge.next()
                    .unwrap()
                    .as_usize()
                    .expect("invalid node id type, should be usize"),
            );

            assert!(
                ENGINE.read().all_nodes.contains_key(&src),
                "node id `{}` doesn't exist",
                src
            );
            assert!(
                ENGINE.read().all_nodes.contains_key(&dst),
                "node id `{}` doesn't exist",
                dst
            );

            // todo: support bi-directional links (be wary of cycles)

            let link = Link {
                to: dst,
                ty: LinkType::Local,
            };
            let mut engine = ENGINE.write();
            let src_node = engine.all_nodes.get_mut(&src).unwrap();
            assert!(
                !src_node.out_links.contains(&link),
                "duplicate edge `{}-{}`",
                src,
                dst
            );
            src_node.out_links.push(link);
        }
    }

    pub fn run() {
        // todo: channel size / unbound ?
        let (sd, rv) = bounded(1024);

        // todo: multiple graphs (low priority)
        let nodes = ENGINE.read().all_nodes.keys().copied().collect();

        sd.send(Message::Data { pkt_id: 0, to: 0 });
        sd.send(Message::Terminate);

        let t = thread::spawn(|| {
            Processor::new(nodes, rv).run();
        });
        t.join();
    }
}

pub enum Message {
    Data { pkt_id: PacketId, to: NodeId },
    Terminate,
}

pub struct Processor {
    nodes: Vec<NodeId>,
    in_ch: Receiver<Message>,
}

impl Processor {
    pub fn new(nodes: Vec<NodeId>, in_ch: Receiver<Message>) -> Self {
        Self { nodes, in_ch }
    }

    pub fn run(&self) {
        for msg in self.in_ch.iter() {
            match msg {
                Message::Data { pkt_id, to } => {
                    self.process(pkt_id, to)
                }
                Message::Terminate => break,
            }
        }
    }

    pub fn process(&self, pkt_id: PacketId, node_id: NodeId) {
        let mut packet = Packet { id: pkt_id }; // todo: get packet from the pool
        unsafe {
            ENGINE
                .read()
                .all_nodes
                .get(&node_id)
                .unwrap()
                .module
                .process(&mut packet as *mut _)
        };

        // todo: non-recursive traversal
        for link in &ENGINE.read().all_nodes.get(&node_id).unwrap().out_links {
            match &link.ty {
                LinkType::Local => self.process(pkt_id, link.to),
                LinkType::Remote(sd) => sd.send(Message::Data {
                    pkt_id,
                    to: link.to,
                }).expect("message should be sent successfully")
            }
        }
    }
}

struct Node {
    module: ModuleTy,
    out_links: Vec<Link>, // todo: smallvec?
}

impl Node {
    pub fn new(module: ModuleTy) -> Self {
        Self {
            module,
            out_links: Vec::new(),
        }
    }
}
