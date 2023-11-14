pub mod factory;
pub mod io;

use std::time::Instant;

use thiserror::Error;

use crate::engine::Context;
use crate::packet::Packet;

pub type ModuleId = usize;
pub type Result<T> = std::result::Result<T, Error>;

// todo: design errors
#[derive(Debug, Error)]
pub enum Error {}

pub trait Module: Send {
    #[allow(unused_variables)]
    fn process(&self, ctx: &Context, packet: *mut Packet) -> Result<()> {
        Ok(())
    }

    #[allow(unused_variables)]
    fn tick(&self, ctx: &Context, now: Instant) -> Result<()> {
        Ok(())
    }
}
