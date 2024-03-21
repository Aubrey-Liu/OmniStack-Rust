use std::cell::OnceCell;
use std::collections::HashMap;

use bitflags::bitflags;
use thiserror::Error;

use crate::engine::Context;
use crate::packet::Packet;

// NOTE: some error types are just here to indicate an event or situation.
// They are not actually errors.
#[derive(Debug, Error)]
pub enum ModuleError {
    #[error("nothing happened")]
    Nop,

    #[error("packet was dropped")]
    Dropped,

    #[error("memory error")]
    MemoryError(#[from] crate::memory::MemoryError),

    #[error("unknown error happened")]
    Unknown,
}

bitflags! {
    pub struct ModuleCapa: u32 {
        const PROCESS = 0b_0001;
        const POLL = 0b_0010;
    }
}

pub type Result<T> = std::result::Result<T, ModuleError>;

#[allow(unused)]
pub trait Module {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        Ok(())
    }

    /// process a packet
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()>;

    /// invoke the module periodically
    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        Err(ModuleError::Nop)
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS
    }

    // TODO: filter
    //
    fn destroy(&mut self, ctx: &Context) {}
}

// TODO: maybe use serde to receive a module is a way?
struct Factory {
    builders: HashMap<&'static str, ModuleBuildFn>,
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

    // It's the very beginning of execution, so log::* is not viable
    // and `panic` also can't be used here.
    if Factory::get().builders.insert(id, Box::new(f)).is_some() {
        println!("warning: module '{}' has already been registered", id);
    }
}

pub(crate) fn build_module(name: &str) -> Box<dyn Module> {
    Factory::get().builders.get(&name).unwrap()()
}
