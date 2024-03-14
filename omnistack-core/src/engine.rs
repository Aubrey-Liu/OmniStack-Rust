use std::ffi::*;
use std::ptr::null_mut;
use std::sync::atomic::AtomicU32;
use std::sync::atomic::Ordering::Relaxed;
use std::thread::JoinHandle;
use std::time::Duration;

use core_affinity::CoreId;
use dpdk_sys as sys;
use smallvec::SmallVec;

use crate::config::{ConfigManager, GraphConfig};
use crate::module::*;
use crate::packet::{Packet, PacketPool};

#[repr(transparent)]
#[derive(Debug, Clone, Copy)]
pub struct NodeId(usize);

const INVALID_NODEID: NodeId = NodeId(usize::MAX);

#[repr(C)]
#[derive(Debug, Clone)]
pub struct Task {
    pub node_id: NodeId,
    pub data: *mut Packet,
}

impl Default for Task {
    fn default() -> Self {
        Self {
            node_id: INVALID_NODEID,
            data: null_mut(),
        }
    }
}

#[repr(C)]
struct Worker {
    task_queue: [Task; Self::TASK_QUEUE_SIZE],

    nodes: SmallVec<[Node; 16]>,
    nodes_to_poll: SmallVec<[NodeId; 4]>,

    id: u32,
    cpu: u32,
    graph_id: u32,
    pktpool: PacketPool,

    stack_name: String,
}

struct Node {
    // id: NodeId,
    module: Box<dyn Module>,
    // TODO: use a forward mask
    links_to: SmallVec<[NodeId; 4]>,
}

#[repr(C)]
#[derive(Debug)]
pub struct Context<'a> {
    pub pktpool: &'a PacketPool,
    pub cpu: u32,
    pub thread_id: u32,
    pub graph_id: u32,

    pub stack_name: &'a str,
}

struct Graph {
    pub id: u32, // Graph's id in the "stack config"
    pub cpu: u32,
    pub nodes: Vec<Node>,
}

// TODO: Build Graph inside worker thread can avoid this
unsafe impl Send for Graph {}

impl Graph {
    pub fn new(id: u32, config: &GraphConfig, cpu: u32) -> Self {
        let modules = &config.modules;

        let mut nodes = Vec::new();

        for (id, module) in config.modules.iter().enumerate() {
            let new_node = Node::from_module(build_module(module));
            nodes.insert(id, new_node);
        }

        for (src, dst) in &config.links {
            let src_idx = modules
                .iter()
                .enumerate()
                .find(|(_, m)| m == &src)
                .map(|(id, _)| id)
                .unwrap();
            let dst_idx = modules
                .iter()
                .enumerate()
                .find(|(_, m)| m == &dst)
                .map(|(id, _)| id)
                .unwrap();

            nodes[src_idx].links_to.push(NodeId(dst_idx));
        }

        Graph { id, nodes, cpu }
    }
}

static THREAD_ID: AtomicU32 = AtomicU32::new(0);

// only the main thread would write this flag, so it's safe
static mut STOP_FLAG: bool = false;

pub struct Engine;

impl Engine {
    pub fn run(stack_name: &str) -> Result<()> {
        // TODO: use less lcore?
        let arg0 = CString::new("").unwrap();
        let arg1 = CString::new("--log-level").unwrap();
        let arg2 = CString::new("debug").unwrap();
        let mut argv = [arg0.as_ptr(), arg1.as_ptr(), arg2.as_ptr()];
        let ret = unsafe { sys::rte_eal_init(argv.len() as _, argv.as_mut_ptr().cast()) };
        if ret < 0 {
            panic!("rte_eal_init failed");
        }

        // TODO: subgraph on different cores?
        let mut graphs = Vec::new();
        let config = ConfigManager::get().lock().unwrap();
        let graph_entries = &config.get_stack_config(stack_name).unwrap().graphs;
        for (id, g) in graph_entries.iter().enumerate() {
            let config = config.get_graph_config(&g.name).unwrap();
            for &cpu in &g.cpus {
                graphs.push(Graph::new(id as _, config, cpu));
            }
        }
        drop(config);

        let mut workers = Self::run_workers(graphs, stack_name)?;
        while let Some(t) = workers.pop() {
            t.join().expect("error joining a thread");
        }

        Ok(())
    }

    fn run_workers(graphs: Vec<Graph>, stack_name: &str) -> Result<Vec<JoinHandle<()>>> {
        let mut threads = Vec::new();

        for g in graphs {
            let stack_name = stack_name.to_string();

            // TODO: give thread a name
            threads.push(std::thread::spawn(move || {
                core_affinity::set_for_current(CoreId { id: g.cpu as _ });

                unsafe { sys::rte_thread_register() };

                let id = THREAD_ID.fetch_add(1, Relaxed);
                let mut worker = Box::new(Worker::new(id, stack_name, g));

                log::debug!("worker {} runs on core {}", worker.id, worker.cpu);

                worker.run();
            }));

            std::thread::sleep(Duration::from_millis(1));
        }

        ctrlc::set_handler(move || unsafe { STOP_FLAG = true })
            .expect("error when setting ctrl-c handler");

        Ok(threads)
    }
}

impl Worker {
    const TASK_QUEUE_SIZE: usize = 2048;

    pub fn new(id: u32, stack_name: String, graph: Graph) -> Self {
        let socket_id = unsafe { sys::rte_socket_id() };

        Self {
            id,
            cpu: graph.cpu as _,
            graph_id: graph.id,
            pktpool: PacketPool::get_or_create(socket_id),

            stack_name,

            nodes: graph.nodes.into(),
            nodes_to_poll: SmallVec::new(),

            task_queue: std::array::from_fn(|_| Task::default()),
        }
    }

    pub fn run(&mut self) {
        let ctx = Context {
            pktpool: &self.pktpool,
            cpu: self.cpu,
            thread_id: self.id,
            graph_id: self.graph_id,
            stack_name: &self.stack_name,
        };

        for (id, node) in self.nodes.iter_mut().enumerate() {
            node.module.init(&ctx).unwrap();

            let capa = node.module.capability();
            assert!(capa.contains(ModuleCapa::PROCESS));
            if capa.contains(ModuleCapa::POLL) {
                self.nodes_to_poll.push(NodeId(id));
            }
        }

        let mut index = 0;

        while unsafe { !STOP_FLAG } {
            for &node_id in &self.nodes_to_poll {
                let node = unsafe { self.nodes.get_unchecked_mut(node_id.0) };

                match node.module.poll(&ctx) {
                    Ok(pkt) => {
                        for &node_id in &node.links_to {
                            let mut pkt = pkt as *mut Packet;

                            while !pkt.is_null() {
                                unsafe {
                                    *self.task_queue.get_unchecked_mut(index) =
                                        Task { data: pkt, node_id };
                                }
                                index += 1;

                                pkt = unsafe { (*pkt).next };
                            }
                        }
                    }
                    Err(ModuleError::NoData) => continue,
                    e @ Err(_) => {
                        let _ = e.unwrap();
                    }
                }
            }

            while index != 0 {
                index -= 1;

                let task = unsafe { self.task_queue.get_unchecked(index) };
                let pkt = unsafe { &mut *task.data };
                let node = unsafe { self.nodes.get_unchecked_mut(task.node_id.0) };

                match node.module.process(&ctx, pkt) {
                    Ok(()) => {
                        pkt.refcnt += node.links_to.len() as u32 - 1;

                        for &node_id in &node.links_to {
                            unsafe {
                                *self.task_queue.get_unchecked_mut(index) =
                                    Task { data: pkt, node_id };
                            }
                            index += 1;
                        }
                    }
                    Err(ModuleError::Done) => continue,
                    e @ Err(_) => e.unwrap(),
                }
            }
        }

        for node in &mut self.nodes {
            node.module.destroy(&ctx);
        }
    }
}

impl Node {
    pub fn from_module(module: Box<dyn Module>) -> Self {
        Self {
            // id,
            module,
            links_to: SmallVec::new(),
        }
    }
}
