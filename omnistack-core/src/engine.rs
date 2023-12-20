use std::collections::HashMap;
use std::ffi::*;
use std::thread::JoinHandle;
use std::time::Instant;

use core_affinity::CoreId;
use crossbeam::channel::{bounded, Receiver, TryRecvError};

use crate::error::Error;
use crate::module::*;
use crate::packet::{Packet, PacketPool};
use crate::Result;

pub type NodeId = usize;
pub type TaskQueue = crossbeam::deque::Worker<Task>;
pub type TaskStealer = crossbeam::deque::Stealer<Task>;

#[derive(Clone, Copy, Debug, PartialEq)]
struct Link {
    to: NodeId,
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
    task_queue: TaskQueue,
    stealers: Vec<TaskStealer>,
    ctrl_ch: Receiver<CtrlMsg>,

    core_id: u16,
    pktpool: PacketPool,
}

pub(crate) struct Node {
    module: Box<dyn Module>,
    out_links: Vec<Link>, // todo: smallvec?
}

unsafe impl Send for Node {}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Context {
    pub core_id: u16,
    pub node: *const c_void,
    pub tq: *const c_void,
    pub pktpool: PacketPool,
}

impl Context {
    #[no_mangle]
    pub extern "C" fn push_task_downstream(&self, data: &mut Packet) {
        let node: *const Node = self.node.cast();
        let links = unsafe { &(*node).out_links };

        data.refcnt += links.len() - 1;

        for link in links {
            self.push_task(Task {
                data,
                node_id: link.to,
            })
        }
    }

    #[no_mangle]
    pub extern "C" fn push_task(&self, task: Task) {
        let tq: *const TaskQueue = self.tq.cast();
        unsafe { (*tq).push(task) };
    }

    /// allocate a packet with data buffer
    #[no_mangle]
    pub extern "C" fn packet_alloc(&self) -> Option<&'static mut Packet> {
        self.pktpool.allocate()
    }

    /// allocate a packet without data buffer
    #[no_mangle]
    pub extern "C" fn meta_packet_alloc(&self) -> Option<&'static mut Packet> {
        self.pktpool.allocate_meta()
    }

    #[no_mangle]
    pub extern "C" fn packet_dealloc(&self, packet: *mut Packet) {
        self.pktpool.deallocate(packet)
    }

    /// allocate a packet without data buffer
    #[no_mangle]
    pub extern "C" fn meta_packet_dealloc(&self, packet: *mut Packet) {
        self.pktpool.deallocate_meta(packet)
    }
}

#[derive(Debug)]
pub enum CtrlMsg {
    ShutDown,
}

unsafe impl Send for CtrlMsg {}

fn dpdk_eal_init() -> Result<()> {
    let arg0 = CString::new("").unwrap();
    let arg1 = CString::new("--log-level=8").unwrap();
    let mut argv = [arg0.as_ptr(), arg1.as_ptr()];
    let ret = unsafe {
        omnistack_sys::dpdk::eal::rte_eal_init(argv.len() as _, argv.as_mut_ptr().cast())
    };
    if ret < 0 {
        return Err(Error::DpdkInitErr);
    }

    Ok(())
}

// maybe allow user to configure this?
// seems unreliable to let Rust detect available cores
const CPUS: usize = 1;

impl Engine {
    pub fn run(config: &str) -> Result<()> {
        dpdk_eal_init()?;

        let mut graphs = Vec::new();
        for _ in 0..CPUS {
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
                Factory::get().contains_name(name),
                "module name `{}` doesn't exist",
                name
            );

            let new_node = Node::new(build_module(name));
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
        let cpus = graphs.len();

        let task_queues: Vec<_> = (0..cpus).map(|_| TaskQueue::new_fifo()).collect();
        let stealers: Vec<_> = (0..cpus)
            .map(|id| {
                task_queues
                    .iter()
                    .enumerate()
                    .filter_map(|(qid, tq)| if id == qid { None } else { Some(tq.stealer()) })
                    .collect()
            })
            .collect();

        for (((id, gp), tq), st) in (0..cpus)
            .zip(graphs)
            .zip(task_queues)
            .zip(stealers)
        {
            let (sd, rv) = bounded(1);
            let mut worker = Worker::new(gp, tq, st, rv, id as _);

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
    pub fn new(
        nodes: Vec<Node>,
        task_queue: TaskQueue,
        stealers: Vec<TaskStealer>,
        ctrl_ch: Receiver<CtrlMsg>,
        core_id: u16,
    ) -> Self {
        Self {
            nodes,
            task_queue,
            stealers,
            ctrl_ch,

            core_id,
            pktpool: PacketPool::get_or_create(core_id as _),
        }
    }

    pub fn run(&mut self) -> Result<()> {
        for id in 0..self.nodes.len() {
            let ctx = self.make_context(id);
            self.nodes[id].module.init(&ctx)?;
        }

        loop {
            while let Some(job) = self.find_job() {
                let pkt = unsafe { job.data.as_mut().unwrap() };
                self.process_with_ctx(job.node_id, pkt)?;
            }

            // todo: use a global mutable variable
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

    fn process_with_ctx(&mut self, node_id: NodeId, pkt: &mut Packet) -> Result<()> {
        let ctx = self.make_context(node_id);
        self.nodes[node_id].module.process(&ctx, pkt)
    }

    fn tick_with_ctx(&mut self, node_id: NodeId) -> Result<()> {
        let ctx = self.make_context(node_id);
        self.nodes[node_id].module.tick(&ctx, Instant::now())
    }

    fn make_context(&self, node_id: NodeId) -> Context {
        Context {
            core_id: self.core_id,
            node: &self.nodes[node_id] as *const _ as *const _,
            tq: &self.task_queue as *const _ as *const _,
            pktpool: self.pktpool,
        }
    }
}

impl Node {
    pub fn new(module: Box<dyn Module>) -> Self {
        Self {
            module,
            out_links: Vec::new(),
        }
    }
}
