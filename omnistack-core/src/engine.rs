use std::ffi::*;
use std::path::Path;
use std::sync::atomic::AtomicU16;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Mutex;
use std::thread::JoinHandle;
use std::time::Duration;

use core_affinity::CoreId;
use crossbeam::channel::{bounded, Receiver, TryRecvError};
use json::JsonValue;

use crate::module::*;
use crate::packet::{Packet, PacketPool};
use crate::protocols::*;
use omnistack_sys as sys;

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
    // NOTE: Node is not cloneable
    nodes: Vec<Node>,
    // NOTE: put ticking nodes together to decrease overhead
    nodes_to_poll: Vec<NodeId>,

    task_queue: TaskQueue,

    // Steal tasks from other cores
    // stealers: Vec<TaskStealer>,
    ctrl_ch: Receiver<CtrlMsg>,

    stack_name: String,
    graph_id: usize,

    id: u16,
    cpu: u16,
    pktpool: PacketPool,
}

// TODO: Prove it's safe to do so
unsafe impl Send for Worker {}

#[allow(dead_code)]
pub(crate) struct Node {
    id: NodeId,
    module: Box<dyn Module>,
    out_links: Vec<Link>, // TODO: smallvec?
}

#[repr(C)]
#[derive(Debug, Clone)]
pub struct Context<'a> {
    pub cpu: u16,
    pub worker_id: u16,
    pub stack_name: &'a str,
    pub graph_id: usize,
    // TODO: use reference
    pub pktpool: &'a PacketPool,
}

impl Context<'_> {
    /// allocate a packet with data buffer
    pub fn allocate(&self) -> Option<&'static mut Packet> {
        self.pktpool.allocate(self.worker_id)
    }

    pub fn deallocate(&self, packet: *mut Packet) {
        // TODO: if a mbuf is attached, also deallocate
        self.pktpool.deallocate(packet, self.worker_id)
    }
}

#[derive(Debug)]
pub enum CtrlMsg {
    ShutDown,
}

pub struct ConfigManager {
    graph_configs: Vec<GraphConfig>,
    stack_configs: Vec<StackConfig>,
}

impl ConfigManager {
    pub fn load_file(path: &Path) {
        let mut cm = Self::get().lock().unwrap();
        let config = std::fs::read_to_string(path).unwrap();
        let config = json::parse(config.as_str()).unwrap();

        let config_type = json_require_str(&config, "type");
        match config_type {
            "Graph" => {
                let graph_config = GraphConfig::from(&config);

                if cm.get_graph_config(&graph_config.name).is_some() {
                    log::error!("Duplicate graph name: {}, skipping ..", graph_config.name);
                    return;
                }

                log::debug!("Loaded graph: {}", graph_config.name);
                cm.graph_configs.push(graph_config);
            }
            "Stack" => {
                let stack_config = StackConfig::from(&config);

                if cm.get_stack_config(&stack_config.name).is_some() {
                    log::error!("Duplicate stack name: {}, skipping ..", stack_config.name);
                    return;
                }

                log::debug!("loaded stack: {}", stack_config.name);
                cm.stack_configs.push(stack_config);
            }
            _ => panic!("invalid config type"),
        }
    }

    pub fn get_graph_config_mut<'a>(&'a mut self, name: &str) -> Option<&'a mut GraphConfig> {
        self.graph_configs.iter_mut().find(|g| g.name == name)
    }

    pub fn get_stack_config_mut<'a>(&'a mut self, name: &str) -> Option<&'a mut StackConfig> {
        self.stack_configs.iter_mut().find(|g| g.name == name)
    }

    pub fn get_graph_config<'a>(&'a self, name: &str) -> Option<&'a GraphConfig> {
        self.graph_configs.iter().find(|g| g.name == name)
    }

    pub fn get_stack_config<'a>(&'a self, name: &str) -> Option<&'a StackConfig> {
        self.stack_configs.iter().find(|g| g.name == name)
    }
}

pub struct GraphConfig {
    pub name: String,
    pub modules: Vec<String>,
    pub links: Vec<(String, String)>,
}

impl From<&JsonValue> for GraphConfig {
    fn from(value: &JsonValue) -> Self {
        let name = json_require_str(value, "name").to_string();
        let modules: Vec<_> = json_require_value(value, "modules")
            .members()
            .map(|m| json_assert_str(m).to_string())
            .collect();
        let links: Vec<_> = json_require_value(value, "links")
            .members()
            .map(|l| {
                (
                    json_assert_str(&l[0]).to_string(),
                    json_assert_str(&l[1]).to_string(),
                )
            })
            .collect();

        Self {
            name,
            modules,
            links,
        }
    }
}

pub struct StackConfig {
    pub name: String,
    pub nics: Vec<NicConfig>,
    pub arps: Vec<ArpEntry>,
    pub routes: Vec<Route>,
    pub graphs: Vec<GraphEntry>,
}

impl From<&JsonValue> for StackConfig {
    fn from(value: &JsonValue) -> Self {
        let name = json_require_str(value, "name").to_string();

        let nics = json_require_value(value, "nics");
        let nics: Vec<_> = nics.members().map(NicConfig::from).collect();

        let arps = json_require_value(value, "arps");
        let arps: Vec<_> = arps.members().map(ArpEntry::from).collect();

        let routes = json_require_value(value, "routes");
        let routes: Vec<_> = routes.members().map(Route::from).collect();

        let graphs = json_require_value(value, "graphs");
        let graphs: Vec<_> = graphs.members().map(GraphEntry::from).collect();

        Self {
            name,
            nics,
            arps,
            routes,
            graphs,
        }
    }
}

pub struct GraphEntry {
    pub name: String,
    pub cpus: Vec<u32>,
}

impl From<&JsonValue> for GraphEntry {
    fn from(value: &JsonValue) -> Self {
        Self {
            name: json_require_str(value, "name").to_string(),
            cpus: json_require_value(value, "cpus")
                .members()
                .map(json_assert_number)
                .collect(),
        }
    }
}

#[derive(Debug)]
pub struct NicConfig {
    pub adapter_name: String,
    pub port: u16,
    pub ipv4: Ipv4Addr,
    pub netmask: Ipv4Addr,
}

impl From<&JsonValue> for NicConfig {
    fn from(value: &JsonValue) -> Self {
        Self {
            adapter_name: json_require_str(value, "adapter_name").to_string(),
            port: json_require_number(value, "port") as _,
            ipv4: json_require_str(value, "ipv4").parse::<Ipv4Addr>().unwrap(),
            netmask: json_require_str(value, "netmask")
                .parse::<Ipv4Addr>()
                .unwrap(),
        }
    }
}

pub struct ArpEntry {
    pub ipv4: Ipv4Addr,
    pub mac: MacAddr,
}

impl From<&JsonValue> for ArpEntry {
    fn from(value: &JsonValue) -> Self {
        let mac: Vec<_> = json_require_str(value, "mac")
            .split(':')
            .map(|s| u8::from_str_radix(s, 16).unwrap())
            .collect();
        let mac = MacAddr::from_bytes(mac.try_into().unwrap());

        Self {
            ipv4: json_require_str(value, "ipv4").parse::<Ipv4Addr>().unwrap(),
            mac,
        }
    }
}

impl From<&JsonValue> for Route {
    fn from(value: &JsonValue) -> Self {
        Self::new(
            json_require_str(value, "ipv4")
                .parse::<Ipv4Addr>()
                .unwrap()
                .to_bits(),
            json_require_number(value, "cidr") as _,
            json_require_number(value, "nic") as _,
        )
    }
}

fn json_require_value<'a>(value: &'a JsonValue, key: &str) -> &'a JsonValue {
    if value[key].is_null() {
        panic!("required field `{}` is missing", key);
    }
    &value[key]
}

fn json_require_number(value: &JsonValue, key: &str) -> u32 {
    let value = json_require_value(value, key);
    if !value.is_number() {
        panic!("required field `{}` is not a number", key);
    }
    value.as_u32().unwrap()
}

fn json_assert_number(value: &JsonValue) -> u32 {
    if !value.is_number() {
        panic!("expect json value {:?} to be a number", value);
    }

    value.as_u32().unwrap()
}

fn json_assert_str(value: &JsonValue) -> &str {
    if !value.is_string() {
        panic!("expect json value {:?} to be a string", value);
    }

    value.as_str().unwrap()
}

fn json_require_str<'a>(value: &'a JsonValue, key: &str) -> &'a str {
    let value = json_require_value(value, key);
    if !value.is_string() {
        panic!("required field `{}` is not a string", key);
    }
    value.as_str().unwrap()
}

impl ConfigManager {
    pub fn get() -> &'static Mutex<ConfigManager> {
        static CONFIG: Mutex<ConfigManager> = Mutex::new(ConfigManager {
            graph_configs: Vec::new(),
            stack_configs: Vec::new(),
        });

        &CONFIG
    }
}

struct Graph {
    pub id: usize, // Graph's id in the "stack config"
    pub nodes: Vec<Node>,
    pub cpu: u32,
}

unsafe impl Send for Graph {}

impl Graph {
    pub fn new(id: usize, config: &GraphConfig, cpu: u32) -> Self {
        let modules = &config.modules;

        let mut nodes = Vec::new();

        for (id, module) in config.modules.iter().enumerate() {
            let new_node = Node::new(id, build_module(module));
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

            nodes[src_idx].out_links.push(Link { to: dst_idx });
        }

        Graph { id, nodes, cpu }
    }
}

impl Engine {
    // TODO: Read graph scheme from config file
    pub fn run(stack_name: &str) -> Result<()> {
        let arg0 = CString::new("").unwrap();
        let arg1 = CString::new("--log-level").unwrap();
        let arg2 = CString::new("debug").unwrap();
        let mut argv = [arg0.as_ptr(), arg1.as_ptr(), arg2.as_ptr()];
        let ret = unsafe { sys::rte_eal_init(argv.len() as _, argv.as_mut_ptr().cast()) };
        if ret < 0 {
            panic!("rte_eal_init failed");
        }

        let mut graphs = Vec::new();
        let config = ConfigManager::get().lock().unwrap();
        let graph_entries = &config.get_stack_config(stack_name).unwrap().graphs;
        for (id, g) in graph_entries.iter().enumerate() {
            let config = config.get_graph_config(&g.name).unwrap();
            for &cpu in &g.cpus {
                graphs.push(Graph::new(id, config, cpu));
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
        let num_graphs = graphs.len();

        let (sd, rv) = bounded(num_graphs); // Channel for ctrl-c signals
        for g in graphs {
            let rv = rv.clone();
            let tq = TaskQueue::new_fifo();
            let stack_name = stack_name.to_string();

            // TODO: give thread a name
            threads.push(std::thread::spawn(move || {
                core_affinity::set_for_current(CoreId { id: g.cpu as _ });

                let mut worker = Worker::new(stack_name, g, tq, rv);
                log::debug!("starting worker {} on core {}", worker.id, worker.cpu);

                worker.run();
            }));

            std::thread::sleep(Duration::from_millis(100));
        }

        ctrlc::set_handler(move || {
            for _ in 0..num_graphs {
                sd.send(CtrlMsg::ShutDown)
                    .expect("cannot send the shutdown CtrlMsg");
            }
        })
        .expect("error when setting ctrl-c handler");

        Ok(threads)
    }
}

static WORKER_ID: AtomicU16 = AtomicU16::new(0);

impl Worker {
    pub fn new(
        stack_name: String,
        graph: Graph,
        task_queue: TaskQueue,
        ctrl_ch: Receiver<CtrlMsg>,
    ) -> Self {
        Self {
            nodes: graph.nodes,
            nodes_to_poll: Vec::new(),

            task_queue,
            ctrl_ch,

            stack_name,
            graph_id: graph.id,

            id: WORKER_ID.fetch_add(1, Relaxed),
            cpu: graph.cpu as _,
            pktpool: PacketPool::get_or_create(graph.cpu as _),
        }
    }

    pub fn run(&mut self) {
        let ctx = Context {
            cpu: self.cpu,
            worker_id: self.id,

            stack_name: &self.stack_name,
            graph_id: self.graph_id,

            pktpool: &self.pktpool,
        };

        for (id, node) in self.nodes.iter_mut().enumerate() {
            node.module.init(&ctx).unwrap();

            let capa = node.module.capability();
            assert!(capa.contains(ModuleCapa::PROCESS));
            if capa.contains(ModuleCapa::POLL) {
                self.nodes_to_poll.push(id);
            }
        }

        let mut num_polls = 0;

        loop {
            while let Some(task) = self.next_task() {
                let node_id = task.node_id;
                let pkt = unsafe { task.data.as_mut().unwrap() };
                let node = &mut self.nodes[node_id];

                match node.module.process(&ctx, pkt) {
                    Ok(_) => {
                        for link in &node.out_links {
                            // Is the task queue inefficient?
                            self.task_queue.push(Task {
                                data: pkt,
                                node_id: link.to,
                            })
                        }
                    }
                    Err(ModuleError::Dropped) => {}
                    e @ Err(_) => e.unwrap(),
                }
            }

            match self.ctrl_ch.try_recv() {
                Ok(msg) => match msg {
                    CtrlMsg::ShutDown => {
                        log::debug!("polled {} times", num_polls);

                        for node in &self.nodes {
                            node.module.destroy(&ctx);
                        }

                        break;
                    }
                },
                Err(TryRecvError::Empty) => {
                    num_polls += 1;

                    for &node_id in &self.nodes_to_poll {
                        let node = &mut self.nodes[node_id];

                        if let Ok(pkt) = node.module.poll(&ctx) {
                            for link in &node.out_links {
                                // Is the task queue inefficient?
                                self.task_queue.push(Task {
                                    data: pkt,
                                    node_id: link.to,
                                })
                            }
                        }
                    }
                }
                Err(TryRecvError::Disconnected) => break,
            }
        }
    }

    fn next_task(&self) -> Option<Task> {
        // TODO: maybe steal from other replica
        self.task_queue.pop()
    }
}

impl Node {
    pub fn new(id: NodeId, module: Box<dyn Module>) -> Self {
        Self {
            id,
            module,
            out_links: Vec::new(),
        }
    }
}
