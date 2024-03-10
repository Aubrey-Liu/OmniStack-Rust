use std::collections::HashMap;

use bitflags::bitflags;
use once_cell::sync::Lazy;
use thiserror::Error;

use crate::engine::Context;
use crate::packet::Packet;

pub type ModuleId = usize;

#[derive(Debug, Error)]
pub enum ModuleError {
    #[error("no incoming packets")]
    NoData,

    #[error("unknown destination address")]
    InvalidDst,

    // NOTE: Modules should take care of dropping packets
    #[error("packet was dropped")]
    Dropped,

    #[error("out of memory")]
    OutofMemory,

    #[error("unknown reasons")]
    Unknown,
}

bitflags! {
    pub struct ModuleCapa: u32 {
        const PROCESS = 0b_0001;
        const POLL = 0b_0010;
    }
}

pub type Result<T> = std::result::Result<T, ModuleError>;

pub trait Module {
    fn init(&mut self, _ctx: &Context) -> Result<()> {
        Ok(())
    }

    /// process a packet
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()>;

    /// invoke the module periodically
    fn poll(&mut self, _ctx: &Context) -> Result<&'static mut Packet> {
        unimplemented!()
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS
    }

    // NOTE: The order of `Drop` can cause problems, so we can only destroy manually.
    fn destroy(&self, _ctx: &Context) {}
}

pub(crate) struct Factory {
    builders: HashMap<&'static str, ModuleBuildFn>,
}

impl Factory {
    pub fn get() -> &'static mut Self {
        static mut FACTORY: Lazy<Factory> = Lazy::new(|| Factory {
            builders: HashMap::new(),
        });

        unsafe { &mut FACTORY }
    }
}

type ModuleBuildFn = Box<dyn Fn() -> Box<dyn Module>>;

// TODO: load dynamic libraries

/// Register a module with its identifier and builder.
///
/// Normally users should not use this method directly,
/// and please use [`register_module`](crate::register_module) instead.
pub fn register<T, F>(id: &'static str, f: F)
where
    T: Module + 'static,
    F: Fn() -> T + 'static,
{
    let f: ModuleBuildFn = Box::new(move || -> Box<dyn Module> {
        let m = f();
        Box::new(m)
    });

    if Factory::get().builders.insert(id, Box::new(f)).is_some() {
        println!("warning: Module '{}' has already been registered", id);
    }
}

pub(crate) fn build_module(name: &str) -> Box<dyn Module> {
    Factory::get().builders.get(&name).unwrap()()
}
