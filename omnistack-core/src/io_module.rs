use std::collections::HashMap;
use std::sync::atomic::AtomicU16;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Mutex;

use once_cell::sync::Lazy;

use crate::engine::{ConfigManager, Context};
use crate::module::*;
use crate::packet::Packet;
use crate::protocols::MacAddr;
use crate::register_module;

pub fn get_mac_addr(nic: u16) -> MacAddr {
    unsafe { NIC_TO_MAC[nic as usize] }
}

const MAX_NIC_NUM: usize = 16;

// NOTE: mut is for performance, and it's not mutable to other modules
static mut NIC_TO_MAC: Vec<MacAddr> = Vec::new();
static NIC_TO_MAC_GUARD: Mutex<()> = Mutex::new(());

pub trait IoAdapter {
    /// Should only be called ONCE.
    fn init(&mut self, ctx: &Context, port_id: u16, num_queues: u16, queue: u16)
        -> Result<MacAddr>;

    fn start(&self) -> Result<()> {
        Ok(())
    }

    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()>;

    fn recv(&mut self, ctx: &Context) -> Result<&'static mut Packet>;

    fn flush(&mut self, _ctx: &Context) -> Result<()> {
        Ok(())
    }

    fn stop(&self, _ctx: &Context) {}
}

struct IoNode {
    // TODO: flush packets periodically
    adapters: Vec<Box<dyn IoAdapter>>,
    num_queues: u16,
}

impl IoNode {
    pub const fn new() -> Self {
        Self {
            adapters: Vec::new(),
            num_queues: 0,
        }
    }
}

static QUEUE_ID: AtomicU16 = AtomicU16::new(0);
static INIT_DONE_QUEUES: AtomicU16 = AtomicU16::new(0);

impl Module for IoNode {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        let mut config = ConfigManager::get().lock().unwrap();
        let stack_config = config.get_stack_config_mut(ctx.stack_name).unwrap();
        let num_queues = stack_config.graphs[ctx.graph_id].cpus.len() as u16;
        let queue_id = QUEUE_ID.fetch_add(1, Relaxed);

        self.num_queues = num_queues;

        let guard = NIC_TO_MAC_GUARD.lock().unwrap();
        unsafe {
            if NIC_TO_MAC.is_empty() {
                NIC_TO_MAC.resize(MAX_NIC_NUM, MacAddr::invalid());
            }
        }
        drop(guard);

        for (id, nic) in stack_config.nics.iter_mut().enumerate() {
            let adapter_name = &nic.adapter_name;
            let builder = Factory::get().builders.get(adapter_name.as_str()).unwrap();
            let mut adapter = builder();

            let mac = adapter.init(ctx, nic.port, num_queues, queue_id).unwrap();

            unsafe {
                NIC_TO_MAC[id] = mac;
            }

            self.adapters.push(adapter);
        }

        // WARNING: DON'T EVER FORGET THIS!
        drop(config);

        if INIT_DONE_QUEUES.load(Relaxed) + 1 == num_queues {
            for adapter in &self.adapters {
                adapter.start().unwrap();
            }
        }
        INIT_DONE_QUEUES.fetch_add(1, Relaxed);

        while INIT_DONE_QUEUES.load(Relaxed) != num_queues {
            std::hint::spin_loop();
        }

        Ok(())
    }

    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        self.adapters[packet.nic as usize].send(ctx, packet)?;

        Err(ModuleError::Dropped)
    }

    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        for (nic, adapter) in self.adapters.iter_mut().enumerate() {
            if let Ok(pkt) = adapter.recv(ctx) {
                pkt.nic = nic as _;
                return Ok(pkt);
            }
        }

        Err(ModuleError::NoData)
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }

    fn destroy(&self, ctx: &Context) {
        for adapter in &self.adapters {
            adapter.stop(ctx);
        }
    }
}

register_module!(IoNode);

#[derive(Default)]
struct Factory {
    builders: HashMap<&'static str, AdapterBuildFn>,
}

impl Factory {
    fn get() -> &'static mut Self {
        static mut FACTORY: Lazy<Factory> = Lazy::new(|| Factory {
            builders: HashMap::new(),
        });

        unsafe { &mut FACTORY }
    }
}

type AdapterBuildFn = Box<dyn Fn() -> Box<dyn IoAdapter>>;

pub fn register<T, F>(id: &'static str, f: F)
where
    T: IoAdapter + 'static,
    F: Fn() -> T + 'static,
{
    let f: AdapterBuildFn = Box::new(move || -> Box<dyn IoAdapter> {
        let m = f();
        Box::new(m)
    });

    if Factory::get().builders.insert(id, Box::new(f)).is_some() {
        println!("warning: IoAdapter '{}' has already been registered", id);
    }
}
