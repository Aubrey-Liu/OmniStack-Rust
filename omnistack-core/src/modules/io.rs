use crate::packet::{Packet, PacketPool};
use crate::register_module;

use super::{Module, Result};

struct Nic {}

impl Nic {
    pub fn new() -> Self {
        Self {}
    }
}

impl Module for Nic {
    fn process(&self, _ctx: &crate::prelude::Context, packet: *mut Packet) -> Result<()> {
        println!("nic sent 1 packet");
        PacketPool::deallocate(packet);
        Ok(())
    }
}

register_module!(Nic, Nic::new);
