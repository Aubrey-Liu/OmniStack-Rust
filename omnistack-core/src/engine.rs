use std::cell::Cell;
use std::collections::HashMap;
use std::thread::JoinHandle;
use std::time::Instant;

use core_affinity::CoreId;
use crossbeam::channel::{bounded, Receiver, TryRecvError};
use crossbeam::deque::{Stealer, Worker as TaskQueue};

use crate::modules::factory::{build_module, get_module, MODULE_FACTORY};
use crate::modules::{ModuleId, Result};
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

#[derive(Clone)]
struct Node {
    module: &'static dyn Module,
    out_links: Vec<Link>, // todo: smallvec?
}

unsafe impl Send for Node {}

pub struct Context<'a> {
    node: &'a Node,
    tasks: &'a TaskQueue<Task>,
}

impl Context<'_> {
    pub fn generate_downsteam_tasks(&self, data: *mut Packet) {
        unsafe { data.as_mut().unwrap().refcnt += self.node.out_links.len() - 1 };

        for link in &self.node.out_links {
            self.push_task(Task {
                data,
                node_id: link.to,
            })
        }
    }

    pub fn push_task(&self, task: Task) {
        self.tasks.push(task)
    }
}

#[derive(Debug)]
pub enum CtrlMsg {
    ShutDown,
}

unsafe impl Send for CtrlMsg {}

impl Engine {
    const NUM_CPUS: usize = 2; // todo: remove hardcode

    pub fn run(config: &str) -> Result<()> {
        PacketPool::init(0);

        let graph = Self::build_graph(config);

        let mut workers = Self::run_workers(graph)?;

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

    fn run_workers(nodes: Vec<Node>) -> Result<Vec<JoinHandle<()>>> {
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

        for ((id, st), tq) in (0..Self::NUM_CPUS).zip(stealers).zip(task_queues) {
            let (sd, rv) = bounded(1024);
            let worker = Worker::new(nodes.clone(), tq, st, rv, id);

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

    pub fn run(&self) -> Result<()> {
        loop {
            while let Some(job) = self.find_job() {
                self.process_job(job)?;
            }

            match self.ctrl_ch.try_recv() {
                Ok(msg) => match msg {
                    CtrlMsg::ShutDown => break,
                },
                Err(TryRecvError::Empty) => {
                    for (id, node) in self.nodes.iter().enumerate() {
                        // todo: make a context
                        let ctx = Context {
                            node: self.nodes.get(id).unwrap(),
                            tasks: &self.task_queue,
                        };
                        node.module.tick(&ctx, Instant::now())?;
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

    fn process_job(&self, job: Task) -> Result<()> {
        let node = self.nodes.get(job.node_id).unwrap();
        let ctx = Context {
            node,
            tasks: &self.task_queue,
        };

        node.module.process(&ctx, job.data)
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
