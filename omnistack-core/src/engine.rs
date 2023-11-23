use std::cell::Cell;
use std::collections::HashMap;
use std::ffi::*;
use std::mem::transmute;
use std::thread::JoinHandle;
use std::time::Instant;

use core_affinity::CoreId;
use crossbeam::channel::{bounded, Receiver, TryRecvError};
use crossbeam::deque::{Stealer, Worker as TaskQueue};

use crate::module_utils::*;
use crate::packet::{Packet, PacketPool};
use crate::prelude::Module;

type NodeId = usize;

#[derive(Clone, Copy, Debug)]
struct Link {
    to: NodeId,
}

impl PartialEq for Link {
    fn eq(&self, other: &Self) -> bool {
        self.to == other.to
    }
}

pub struct Engine;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Task {
    pub data: *mut Packet,
    pub node_id: NodeId,
}

unsafe impl Send for Task {}

struct Worker {
    nodes: Vec<Node>,
    task_queue: TaskQueue<Task>,
    stealers: Vec<Stealer<Task>>,
    ctrl_ch: Receiver<CtrlMsg>,
}

pub(crate) struct Node {
    module: &'static mut dyn Module,
    out_links: Vec<Link>, // todo: smallvec?
}

unsafe impl Send for Node {}

#[repr(C)]
pub struct Context {
    pub node: *const c_void,
    pub tq: *const c_void,
    pub pktpool: PacketPool,
}

impl Context {
    #[no_mangle]
    pub extern "C" fn push_task_downstream(&self, data: *mut Packet) {
        let node: *const Node = self.node.cast();
        let links = unsafe { &(&*node).out_links };

        unsafe { (&mut *data).refcnt += links.len() - 1 };

        for link in links {
            self.push_task(Task {
                data,
                node_id: link.to,
            })
        }
    }

    #[no_mangle]
    pub extern "C" fn push_task(&self, task: Task) {
        let tq: *const TaskQueue<Task> = self.tq.cast();
        unsafe { (&*tq).push(task) }
    }

    #[no_mangle]
    pub extern "C" fn packet_alloc(&self) -> Option<&mut Packet> {
        self.pktpool.allocate()
    }

    #[no_mangle]
    pub extern "C" fn packet_dealloc(&self, packet: *mut Packet) {
        self.pktpool.deallocate(packet)
    }
}

#[derive(Debug)]
pub enum CtrlMsg {
    ShutDown,
}

unsafe impl Send for CtrlMsg {}

impl Engine {
    const NUM_CORES: usize = 1; // todo: remove hardcode

    pub fn run(config: &str) -> Result<()> {
        let arg0 = CString::new("").unwrap();
        let argv = [arg0.as_ptr()];
        let ret = unsafe { omnistack_sys::dpdk::eal::rte_eal_init(1, transmute(argv)) };
        if ret < 0 {
            return Err(Error::DpdkInitErr);
        }

        let mut graphs = Vec::new();
        for _ in 0..Self::NUM_CORES {
            graphs.push(Self::build_graph(config))
        }

        let mut workers = Self::run_workers(graphs)?;

        while let Some(t) = workers.pop() {
            t.join().expect("error joining a thread");
        }

        Ok(())
    }

    fn build_graph(config: &str) -> Vec<Node> {
        let config = json::parse(config).expect("config should be in json format");
        let nodes = &config["nodes"];
        let edges = &config["edges"];
        let mut graph = Vec::new();
        let mut node_ids = HashMap::new();

        assert!(!nodes.is_null(), "key `nodes` not found in config");
        assert!(!edges.is_null(), "key `edges` not found in config");

        for node in nodes.members() {
            assert!(!node["id"].is_null(), "node doesn't have key `id`");
            assert!(!node["name"].is_null(), "node doesn't have key `name`");

            let id = node["id"]
                .as_str()
                .expect(r#"invalie node["id"] type, shoule be str"#);
            let name = node["name"]
                .as_str()
                .expect(r#"invalie node["name"] type, shoule be str"#);

            assert!(!node_ids.contains_key(&id), "duplicate node id `{}`", id);
            assert!(
                unsafe { MODULE_FACTORY.contains_key(&name) },
                "module name `{}` doesn't exist",
                name
            );

            let new_node = Node::new(build_module(name));
            new_node.module.init().unwrap();
            let node_id = graph.len();
            node_ids.insert(id, node_id);
            graph.insert(node_id, new_node);
        }

        for edge in edges.members() {
            assert!(edge.is_array() && edge.members().len() == 2);
            let mut edge = edge.members();
            let (src, dst) = (
                edge.next()
                    .unwrap()
                    .as_str()
                    .expect("invalid node id type, should be str"),
                edge.next()
                    .unwrap()
                    .as_str()
                    .expect("invalid node id type, should be str"),
            );

            assert!(
                node_ids.contains_key(&src),
                "node id `{}` doesn't exist",
                src
            );
            assert!(
                node_ids.contains_key(&dst),
                "node id `{}` doesn't exist",
                dst
            );

            let src_id = node_ids.get(&src).copied().unwrap();
            let dst_id = node_ids.get(&dst).copied().unwrap();

            // todo: support bi-directional links (be wary of cycles)
            let link = Link { to: dst_id };

            let src_node = graph.get_mut(src_id).unwrap();
            assert!(
                !src_node.out_links.contains(&link),
                "duplicate edge `{}-{}`",
                src,
                dst
            );
            src_node.out_links.push(link);
        }

        graph
    }

    fn run_workers(graphs: Vec<Vec<Node>>) -> Result<Vec<JoinHandle<()>>> {
        let mut threads = Vec::new();
        let mut ctrl_chs = Vec::new();

        let task_queues: Vec<_> = (0..Self::NUM_CORES)
            .map(|_| TaskQueue::new_fifo())
            .collect();
        let stealers: Vec<_> = (0..Self::NUM_CORES)
            .map(|id| {
                task_queues
                    .iter()
                    .enumerate()
                    .filter_map(|(qid, tq)| if id == qid { None } else { Some(tq.stealer()) })
                    .collect()
            })
            .collect();

        for (((id, gp), tq), st) in (0..Self::NUM_CORES)
            .zip(graphs)
            .zip(task_queues)
            .zip(stealers)
        {
            let (sd, rv) = bounded(1);
            let mut worker = Worker::new(gp, tq, st, rv, id);

            threads.push(std::thread::spawn(move || {
                core_affinity::set_for_current(CoreId { id });
                worker.run().unwrap(); // todo: handle errors gracefully
            }));

            ctrl_chs.push(sd);
        }

        ctrlc::set_handler(move || {
            for ch in &ctrl_chs {
                ch.send(CtrlMsg::ShutDown)
                    .expect("cannot send the shutdown CtrlMsg");
            }
        })
        .expect("error when setting ctrl-c handler");

        Ok(threads)
    }
}

impl Worker {
    thread_local! {
        static CORE_ID: Cell<i32> = Cell::new(-1);
    }

    pub fn new(
        nodes: Vec<Node>,
        task_queue: TaskQueue<Task>,
        stealers: Vec<Stealer<Task>>,
        ctrl_ch: Receiver<CtrlMsg>,
        core_id: usize,
    ) -> Self {
        Self::CORE_ID.set(core_id as i32);

        Self {
            nodes,
            task_queue,
            stealers,
            ctrl_ch,
        }
    }

    pub fn run(&mut self) -> Result<()> {
        loop {
            while let Some(job) = self.find_job() {
                self.process_with_ctx(job.node_id, job.data)?;
            }

            match self.ctrl_ch.try_recv() {
                Ok(msg) => match msg {
                    CtrlMsg::ShutDown => break,
                },
                Err(TryRecvError::Empty) => {
                    for node_id in 0..self.nodes.len() {
                        self.tick_with_ctx(node_id)?;
                    }
                }
                Err(TryRecvError::Disconnected) => break,
            }
        }

        Ok(())
    }

    fn find_job(&self) -> Option<Task> {
        self.task_queue.pop().or_else(|| {
            self.stealers
                .iter()
                .map(|s| s.steal_batch_with_limit_and_pop(&self.task_queue, 8))
                .find_map(|j| j.success())
        })
    }

    fn process_with_ctx(&mut self, node_id: NodeId, pkt: *mut Packet) -> Result<()> {
        let node = self.nodes.get_mut(node_id).unwrap();
        let ctx = Context {
            node: &node as *const _ as *const _,
            tq: &self.task_queue as *const _ as *const _,
            pktpool: PacketPool::get_or_create("mp"),
        };

        node.module.process(&ctx, pkt)
    }

    fn tick_with_ctx(&mut self, node_id: NodeId) -> Result<()> {
        let node = self.nodes.get_mut(node_id).unwrap();
        let ctx = Context {
            node: &node as *const _ as *const _,
            tq: &self.task_queue as *const _ as *const _,
            pktpool: PacketPool::get_or_create("mp"),
        };

        node.module.tick(&ctx, Instant::now())
    }
}

impl Node {
    pub fn new(module_id: ModuleId) -> Self {
        Self {
            module: get_module(module_id).unwrap(),
            out_links: Vec::new(),
        }
    }
}
