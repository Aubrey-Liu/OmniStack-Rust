use std::collections::HashMap;
use std::sync::atomic::AtomicU16;
use std::sync::atomic::Ordering::Relaxed;

use once_cell::sync::Lazy;

use omnistack_core::config::ConfigManager;
use omnistack_core::engine::Context;
use omnistack_core::module::*;
use omnistack_core::packet::Packet;
use omnistack_core::protocols::MacAddr;
use omnistack_core::register_module;
use smallvec::SmallVec;

const MAX_NIC: usize = 16;

#[inline(always)]
pub fn nic_to_mac(nic: u16) -> MacAddr {
    unsafe { NIC_TO_MAC[nic as usize] }
}

// NOTE: mut is for performance, and it's not mutable to other modules
static mut NIC_TO_MAC: Vec<MacAddr> = Vec::new();

pub trait IoAdapter {
    /// Should only be called ONCE.
    fn init(
        &mut self,
        ctx: &Context,
        nic: u16,
        port_id: u16,
        num_queues: u16,
        queue: u16,
    ) -> Result<MacAddr>;

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

const NUM_ADAPTER_MAX: usize = 16;

struct IoNode {
    flush_queue: [u8; NUM_ADAPTER_MAX],
    flush_queue_idx: usize,

    flush_flags: [bool; NUM_ADAPTER_MAX],

    adapters: SmallVec<[Box<dyn IoAdapter>; NUM_ADAPTER_MAX]>,
}

impl IoNode {
    #[allow(invalid_value)]
    pub fn new() -> Self {
        Self {
            flush_queue: [0; NUM_ADAPTER_MAX],
            flush_queue_idx: 0,

            flush_flags: [false; NUM_ADAPTER_MAX],

            adapters: SmallVec::new(),
        }
    }
}

static QUEUE_ID: AtomicU16 = AtomicU16::new(0);
static INIT_DONE_QUEUES: AtomicU16 = AtomicU16::new(0);

impl Module for IoNode {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        // this lock guarantees the following steps are not concurrent
        let config = ConfigManager::get().lock().unwrap();
        let stack_config = config.get_stack_config(ctx.stack_name).unwrap();
        let num_queues = stack_config.graphs[ctx.graph_id as usize].cpus.len() as u16;
        let queue_id = QUEUE_ID.fetch_add(1, Relaxed);

        unsafe {
            if NIC_TO_MAC.is_empty() {
                NIC_TO_MAC.resize(MAX_NIC, MacAddr::invalid());
            }
        }

        for (id, nic) in stack_config.nics.iter().enumerate() {
            let adapter_name = &nic.adapter_name;
            let builder = Factory::get().builders.get(adapter_name.as_str()).unwrap();
            let mut adapter = builder();

            // TODO: Return Recv Queue
            let mac = adapter
                .init(ctx, id as _, nic.port, num_queues, queue_id)
                .unwrap();

            unsafe {
                NIC_TO_MAC[id] = mac;
            }

            self.adapters.push(adapter);
        }

        // WARNING: DON'T EVER FORGET THIS!
        drop(config);

        // start adapter after all initialization are done
        if INIT_DONE_QUEUES.load(Relaxed) + 1 == num_queues {
            for adapter in &self.adapters {
                adapter.start()?;
            }
        }
        INIT_DONE_QUEUES.fetch_add(1, Relaxed);

        // wait until every queue is initialized
        while INIT_DONE_QUEUES.load(Relaxed) != num_queues {
            std::hint::spin_loop();
        }

        Ok(())
    }

    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        let nic = packet.nic as usize;
        unsafe {
            self.adapters.get_unchecked_mut(nic).send(ctx, packet)?;
        }

        if unsafe { !*self.flush_flags.get_unchecked(nic) } {
            unsafe {
                *self.flush_flags.get_unchecked_mut(nic) = true;
                *self.flush_queue.get_unchecked_mut(self.flush_queue_idx) = nic as u8;
            }
            self.flush_queue_idx += 1;
        }

        Err(ModuleError::Done)
    }

    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        while self.flush_queue_idx != 0 {
            self.flush_queue_idx -= 1;

            let nic_to_flush = self.flush_queue[self.flush_queue_idx];
            unsafe {
                self.adapters
                    .get_unchecked_mut(nic_to_flush as usize)
                    .flush(ctx)?;
                *self.flush_flags.get_unchecked_mut(nic_to_flush as usize) = false;
            };
        }

        // TODO: flush
        #[cfg(feature = "recv")]
        {
            for adapter in &mut self.adapters {
                match adapter.recv(ctx) {
                    p @ Ok(_) => return p,
                    Err(ModuleError::NoData) => continue,
                    e @ Err(_) => return e,
                }
            }
        }

        Err(ModuleError::NoData)
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }

    fn destroy(&mut self, ctx: &Context) {
        for adapter in &mut self.adapters {
            let _ = adapter.flush(ctx);
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
