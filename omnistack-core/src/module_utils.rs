use std::collections::HashMap;
use std::fmt::Debug;
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
    Unknown,
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

pub struct Factory {
    builders: HashMap<&'static str, ModuleBuildFn>,
}

impl Factory {
    pub fn get() -> &'static mut Self {
        static mut FACTORY: Lazy<Factory> = Lazy::new(|| Factory {
            builders: HashMap::new(),
        });

        unsafe { &mut FACTORY }
    }

    pub fn contains_name(&self, name: &str) -> bool {
        self.builders.contains_key(name)
    }
}

type ModuleBuildFn = Box<dyn Fn() -> Box<dyn Module>>;

// todo: load dynamic libraries

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

    if Factory::get()
        .builders
        .insert(id, Box::new(f))
        .is_some()
    {
        println!("warning: Module '{}' has already been registered", id);
    }
}

pub(crate) fn build_module(name: &str) -> Box<dyn Module> {
    Factory::get().builders.get(&name).unwrap()()
}
