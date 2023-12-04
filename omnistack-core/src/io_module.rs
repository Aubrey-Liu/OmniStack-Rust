use std::collections::HashMap;
use std::time::Instant;

use once_cell::sync::Lazy;

use crate::engine::Context;
use crate::module::Module;
use crate::packet::Packet;
use crate::register_module;

// use omnistack_sys::dpdk as sys;

use crate::Result;

pub trait IoAdapter {
    fn init(&mut self, ctx: &Context, port_id: u16, num_queues: u16) -> Result<()>;

    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()>;

    fn recv(&mut self, ctx: &Context) -> Result<&mut Packet>;
}

struct IoNode {
    adapters: Vec<Box<dyn IoAdapter>>,
}

impl IoNode {
    pub const fn new() -> Self {
        Self {
            adapters: Vec::new(),
        }
    }
}

impl Module for IoNode {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        for f in Factory::get().builders.values() {
            self.adapters.push(f());
        }
        // todo: collect mac addresses
        self.adapters
            .iter_mut()
            .try_for_each(|io| io.init(ctx, 0, 1))?;

        Ok(())
    }

    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        self.adapters[packet.nic as usize].send(ctx, packet)
    }

    fn tick(&mut self, ctx: &Context, _now: Instant) -> Result<()> {
        self.adapters.iter_mut().enumerate().for_each(|(nic, io)| {
            if let Ok(pkt) = io.recv(ctx) {
                pkt.nic = nic as _;
                ctx.push_task_downstream(pkt);
            }
        });

        Ok(())
    }
}

register_module!(IoNode);

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
