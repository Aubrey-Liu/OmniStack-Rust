use std::ffi::c_void;
use std::ptr::null_mut;

use arrayvec::ArrayVec;
use dpdk_sys as sys;

use crate::config::{ConfigManager, GraphConfig};
use crate::memory::{next_thread_id, ThreadId};
use crate::module::{build_module, Module, ModuleCapa, ModuleError, Result};
use crate::packet::{Packet, PacketPool};
use crate::sys::process_init;
use crate::user::Server;

#[repr(transparent)]
#[derive(Debug, Clone, Copy)]
struct NodeId(usize);

const INVALID_NODEID: NodeId = NodeId(usize::MAX);

#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct Task {
    node_id: NodeId,
    data: *mut Packet,
}

impl Default for Task {
    fn default() -> Self {
        Self {
            node_id: INVALID_NODEID,
            data: null_mut(),
        }
    }
}

#[repr(C, align(64))]
struct Worker {
    task_queue: [Task; Self::TASK_QUEUE_SIZE],

    nodes: ArrayVec<Node, 32>,
    nodes_to_poll: ArrayVec<NodeId, 8>,

    id: u32, // worker id
    graph_id: u32,
    cpu: u32,

    stack_name: String,
}

#[repr(C, align(64))]
struct Node {
    // id: NodeId,
    module: Box<dyn Module>,
    // TODO: use a forward mask
    links_to: ArrayVec<NodeId, 4>,
}

unsafe impl Send for Node {}

#[repr(C, align(64))]
pub struct Context<'a> {
    pub pktpool: &'a PacketPool,
    pub cpu: u32,
    pub worker_id: u32,
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

// only the main thread would write this flag, so it's safe
pub(crate) static mut STOP_FLAG: bool = false;

// TODO: don't need an Engine struct, can just use some functions
pub struct Engine;

impl Engine {
    pub fn run(stack_name: &str) -> Result<()> {
        // TODO: subgraph on different cores?
        let config = ConfigManager::get().lock().unwrap();

        let graph_entries = if let Some(s) = config.get_stack_config(stack_name) {
            &s.graphs
        } else {
            panic!("unknown stack name '{}'", stack_name);
        };

        let mut graphs = Vec::new();
        for (id, g) in graph_entries.iter().enumerate() {
            let config = config.get_graph_config(&g.name).unwrap();
            for &cpu in &g.cpus {
                graphs.push(Graph::new(id as _, config, cpu));
            }
        }

        drop(config);

        // init dpdk
        process_init();

        ctrlc::set_handler(move || unsafe { STOP_FLAG = true })
            .expect("error when setting ctrl-c handler");

        Self::run_workers(graphs, stack_name);

        let server = std::thread::Builder::new()
            .name("Coordinator".to_string())
            .spawn(Server::run)
            .unwrap();

        unsafe { sys::rte_eal_mp_wait_lcore() };
        let _ = server.join();

        // unsafe { sys::rte_eal_cleanup() };

        Ok(())
    }

    fn run_workers(graphs: Vec<Graph>, stack_name: &str) {
        static mut WORKERS: Vec<Worker> = Vec::new();

        unsafe extern "C" fn worker_routine(worker: *mut c_void) -> i32 {
            let worker = unsafe { worker.cast::<Worker>().as_mut().unwrap() };

            // core_affinity::set_for_current(core_affinity::CoreId {
            //     id: worker.cpu as usize,
            // });

            assert_eq!(sys::rte_lcore_id(), worker.cpu);

            log::info!("Worker {} runs on core {}", worker.id, worker.cpu);

            worker.run();

            0
        }

        for (id, g) in graphs.into_iter().enumerate() {
            let cpu = g.cpu;
            let stack_name = stack_name.to_string();

            unsafe { WORKERS.insert(id, Worker::new(id as _, stack_name, g.id, g.nodes, cpu)) };

            let worker_ptr = unsafe { WORKERS.get_mut(id).unwrap() };

            unsafe {
                sys::rte_eal_remote_launch(
                    Some(worker_routine),
                    worker_ptr as *mut _ as *mut _,
                    cpu,
                )
            };
        }
    }
}

impl Worker {
    const TASK_QUEUE_SIZE: usize = 2048;

    pub fn new(id: u32, stack_name: String, graph_id: u32, nodes: Vec<Node>, cpu: u32) -> Self {
        let mut nodes_arr = ArrayVec::new();
        for node in nodes {
            nodes_arr.try_push(node).unwrap();
        }

        Self {
            task_queue: [Task::default(); Self::TASK_QUEUE_SIZE],

            id,
            graph_id,
            cpu,

            stack_name,

            nodes: nodes_arr,
            nodes_to_poll: ArrayVec::new(),
        }
    }

    pub fn run(&mut self) {
        let thread_id = next_thread_id();
        ThreadId::set(thread_id);

        let socket_id = unsafe { sys::rte_socket_id() };
        let pktpool = PacketPool::get_or_create(socket_id);

        let ctx = Context {
            pktpool: &pktpool,
            cpu: self.cpu,
            worker_id: self.id,
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
                    Err(ModuleError::Nop) => continue,
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
                    Err(ModuleError::Dropped) => continue,
                    e @ Err(_) => e.unwrap(),
                }
            }
        }

        for node in &mut self.nodes {
            node.module.destroy(&ctx);
        }

        // make sure all threads would exit
        unsafe { STOP_FLAG = true };
    }
}

impl Node {
    pub fn from_module(module: Box<dyn Module>) -> Self {
        Self {
            module,
            links_to: ArrayVec::new(),
        }
    }
}
