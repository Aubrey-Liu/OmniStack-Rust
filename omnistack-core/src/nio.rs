use std::cell::OnceCell;
use std::collections::HashMap;
use std::sync::atomic::AtomicU16;
use std::sync::atomic::Ordering::*;
use std::sync::Mutex;

use arrayvec::ArrayVec;

use crate::config::ConfigManager;
use crate::engine::Context;
use crate::memory::Aligned;
use crate::module::*;
use crate::packet::Packet;
use crate::prelude::Error;
use crate::protocol::{EthHeader, MacAddr};
use crate::Result;

#[allow(unused)]
pub trait Adapter {
    fn init(
        &mut self,
        ctx: &Context,
        nic: u16,
        port_id: u16,
        num_queues: u16,
        queue: u16,
    ) -> Result<MacAddr> {
        Ok(MacAddr::new())
    }

    fn start(&self) -> Result<()> {
        Ok(())
    }

    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        Err(Error::Nop)
    }

    fn flush(&mut self, ctx: &Context) -> Result<()> {
        Ok(())
    }

    fn stop(&self, ctx: &Context) {}
}

const NUM_ADAPTER_MAX: usize = 16;

#[repr(C, align(64))]
#[derive(omnistack_proc::Module)]
struct IoNode {
    flush_queue: [u8; NUM_ADAPTER_MAX],
    flush_flag: usize,
    flush_queue_idx: usize,

    adapters: Aligned<ArrayVec<Box<dyn Adapter>, NUM_ADAPTER_MAX>>,

    nic_to_mac: [MacAddr; NUM_ADAPTER_MAX],
}

impl IoNode {
    pub fn new() -> Self {
        Self {
            flush_queue: [0; NUM_ADAPTER_MAX],
            flush_flag: 0,
            flush_queue_idx: 0,

            adapters: ArrayVec::new().into(),
            nic_to_mac: [MacAddr::new(); NUM_ADAPTER_MAX],
        }
    }
}

impl Module for IoNode {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        static QUEUE_ID: AtomicU16 = AtomicU16::new(0);

        let config = ConfigManager::get();
        let stack_config = config.get_stack_config(ctx.stack_name).unwrap();
        let num_queues = stack_config.graphs[ctx.graph_id as usize].cpus.len() as u16;
        let queue_id = QUEUE_ID.fetch_add(1, Relaxed);

        for (id, nic) in stack_config.nics.iter().enumerate() {
            let adapter_name = &nic.adapter_name;
            let builder = Factory::get().builders.get(adapter_name.as_str()).unwrap();
            let mut adapter = builder();

            // TODO: Return Recv Queue
            let mac = adapter
                .init(ctx, id as _, nic.port, num_queues, queue_id)
                .unwrap();

            self.nic_to_mac[id] = mac;
            log::debug!(
                "[Thread {}] MAC addr of nic {} is {:?}",
                crate::service::ThreadId::get(),
                id,
                mac
            );

            self.adapters.0.push(adapter);
        }

        Ok(())
    }

    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        let nic = packet.nic as usize;
        let ethh = packet.get_l2_header::<EthHeader>();

        unsafe {
            ethh.src = *self.nic_to_mac.get_unchecked(nic);
            self.adapters.0.get_unchecked_mut(nic).send(ctx, packet)?;
        }

        let mask = 1 << nic;
        if self.flush_flag & mask == 0 {
            unsafe { *self.flush_queue.get_unchecked_mut(self.flush_queue_idx) = nic as u8 };
            self.flush_flag |= mask;
            self.flush_queue_idx += 1;
        }

        Err(Error::Dropped)
    }

    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        while self.flush_queue_idx != 0 {
            self.flush_queue_idx -= 1;

            let nic_to_flush = unsafe { *self.flush_queue.get_unchecked(self.flush_queue_idx) };
            self.flush_flag ^= (1 << nic_to_flush) as usize;

            unsafe {
                self.adapters
                    .0
                    .get_unchecked_mut(nic_to_flush as usize)
                    .flush(ctx)?;
            };
        }

        for adapter in &mut self.adapters.0 {
            match adapter.recv(ctx) {
                Err(Error::Nop) => continue,
                r => return r,
            }
        }

        Err(Error::Nop)
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }

    fn destroy(&mut self, ctx: &Context) {
        for adapter in &mut self.adapters.0 {
            let _ = adapter.flush(ctx);
            adapter.stop(ctx);
        }
    }
}

struct Factory {
    builders: HashMap<&'static str, AdapterBuildFn>,
}

impl Factory {
    fn get() -> &'static mut Self {
        static mut FACTORY: OnceCell<Factory> = OnceCell::new();

        unsafe {
            FACTORY.get_or_init(|| Factory {
                builders: HashMap::new(),
            });
        }

        unsafe { FACTORY.get_mut().unwrap() }
    }
}

type AdapterBuildFn = Box<dyn Fn() -> Box<dyn Adapter>>;

pub fn register<T, F>(id: &'static str, f: F)
where
    T: Adapter + 'static,
    F: Fn() -> T + 'static,
{
    let f: AdapterBuildFn = Box::new(move || -> Box<dyn Adapter> {
        let m = f();
        Box::new(m)
    });

    if Factory::get().builders.insert(id, Box::new(f)).is_some() {
        println!("warning: IoAdapter '{}' has already been registered", id);
    }
}
