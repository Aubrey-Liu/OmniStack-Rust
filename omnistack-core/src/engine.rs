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

pub struct Context {
    pub(crate) node: *const Node,
    pub(crate) tq: *const TaskQueue<Task>,
}

impl Context {
    pub fn push_task_downstream(&self, data: *mut Packet) {
        let links = unsafe { &(&*self.node).out_links };

        unsafe { data.as_mut().unwrap().refcnt += links.len() - 1 };

        for link in links {
            self.push_task(Task {
                data,
                node_id: link.to,
            })
        }
    }

    pub fn push_task(&self, task: Task) {
        unsafe { (&*self.tq).push(task) }
    }
}

#[derive(Debug)]
pub enum CtrlMsg {
    ShutDown,
}

unsafe impl Send for CtrlMsg {}

impl Engine {
    const NUM_CPUS: usize = 4; // todo: remove hardcode

    pub fn run(config: &str) -> Result<()> {
        extern "C" {
            fn rte_eal_init(argc: c_int, argv: *mut *mut c_char) -> c_int;
        }

        let arg0 = CString::new("").unwrap();
        let argv = [arg0.as_ptr()];
        let ret = unsafe { rte_eal_init(1, transmute(argv)) };
        if ret < 0 {
            return Err(Error::DpdkInitErr);
        }

        PacketPool::init(Self::NUM_CPUS);

        let mut graphs = Vec::new();
        for _ in 0..Self::NUM_CPUS {
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

        let task_queues: Vec<_> = (0..Self::NUM_CPUS).map(|_| TaskQueue::new_fifo()).collect();
        let stealers: Vec<_> = (0..Self::NUM_CPUS)
            .map(|id| {
                task_queues
                    .iter()
                    .enumerate()
                    .filter_map(|(qid, tq)| if id == qid { None } else { Some(tq.stealer()) })
                    .collect()
            })
            .collect();

        for (((id, gp), tq), st) in (0..Self::NUM_CPUS)
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
                self.process_job(job)?;
            }

            match self.ctrl_ch.try_recv() {
                Ok(msg) => match msg {
                    CtrlMsg::ShutDown => break,
                },
                Err(TryRecvError::Empty) => {
                    for id in 0..self.nodes.len() {
                        let ctx = self.make_context(id);
                        self.nodes
                            .get_mut(id)
                            .unwrap()
                            .module
                            .tick(&ctx, Instant::now())?;
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

    fn process_job(&mut self, job: Task) -> Result<()> {
        let node = self.nodes.get_mut(job.node_id).unwrap();
        let ctx = Context {
            node: node as *const _,
            tq: &self.task_queue,
        };

        node.module.process(&ctx, job.data)
    }

    fn make_context(&self, node_id: NodeId) -> Context {
        Context {
            node: self.nodes.get(node_id).unwrap() as *const _,
            tq: &self.task_queue,
        }
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
