use std::collections::HashMap;
use std::time::Instant;

use once_cell::sync::Lazy;
use thiserror::Error;

use crate::engine::Context;
use crate::packet::Packet;

pub type ModuleId = usize;
pub type Result<T> = std::result::Result<T, Error>;

// todo: design errors
#[derive(Debug, Error)]
pub enum Error {
    #[error("failed to init dpdk")]
    DpdkInitErr,

    #[error("unknown errors")]
    Unknown
}

pub trait Module: Send {
    fn init(&mut self) -> Result<()> {
        Ok(())
    }

    /// process a packet
    #[allow(unused_variables)]
    fn process(&mut self, ctx: &Context, packet: *mut Packet) -> Result<()> {
        Ok(())
    }

    /// do something periodically
    #[allow(unused_variables)]
    fn tick(&mut self, ctx: &Context, now: Instant) -> Result<()> {
        Ok(())
    }
}

type ModuleBuildFn = dyn Fn() -> Box<dyn Module>;

pub(crate) static mut MODULE_FACTORY: Lazy<HashMap<&'static str, Box<ModuleBuildFn>>> =
    Lazy::new(HashMap::new);

pub(crate) static mut MODULES: Lazy<HashMap<ModuleId, Box<dyn Module>>> = Lazy::new(HashMap::new);

/// Register a module with its identifier and builder.
///
/// Normally users should not use this method directly,
/// and please use [`register_module`](crate::register_module) instead.
pub fn register<T, F>(id: &'static str, f: F)
where
    T: Module + 'static,
    F: Fn() -> T + 'static,
{
    let f = move || -> Box<dyn Module> {
        let m = f();
        Box::new(m)
    };

    if unsafe { MODULE_FACTORY.insert(id, Box::new(f)).is_some() } {
        println!("warning: Module '{}' has already been registered", id);
    }
}

pub fn build_module(name: &str) -> ModuleId {
    let m = unsafe { MODULE_FACTORY.get(&name).unwrap()() };
    let id = unsafe { MODULES.len() };
    unsafe { MODULES.insert(id, m) };
    id
}

#[inline(always)]
pub fn get_module(id: ModuleId) -> Option<&'static mut dyn Module> {
    unsafe { MODULES.get_mut(&id).map(|m| m.as_mut()) }
}
