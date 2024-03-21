use crate::prelude::*;
use crate::protocol::{EthHeader, EtherType, MacAddr};

#[derive(omnistack_proc::Module)]
struct EthSender;

#[derive(omnistack_proc::Module)]
struct EthReceiver;

impl EthSender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for EthSender {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l2_header.length = std::mem::size_of::<EthHeader>() as _;
        packet.offset -= packet.l2_header.length;
        packet.l2_header.offset = packet.offset as _;

        let eth_header = packet.get_l2_header::<EthHeader>();
        eth_header.dst = MacAddr::from_bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x00]);
        // Moved this into the Io node.
        // eth_header.src = unsafe { nic_to_mac(packet.nic) };
        eth_header.ether_ty = EtherType::Ipv4;

        Ok(())
    }
}

impl EthReceiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for EthReceiver {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l2_header.length = std::mem::size_of::<EthHeader>() as _;
        packet.l2_header.offset = packet.offset;
        packet.offset += packet.l2_header.length;

        Ok(())
    }
}
