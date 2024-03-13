use omnistack_core::prelude::*;
use omnistack_core::protocols::{EthHeader, EtherType, MacAddr};

use crate::modules::io::nic_to_mac;

struct EthSender;
struct EthReceiver;

impl EthSender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for EthSender {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l2_header.length = std::mem::size_of::<EthHeader>() as _;
        packet.offset -= packet.l2_header.length as u16;
        packet.l2_header.offset = packet.offset as _;

        let eth_header = packet.get_l2_header::<EthHeader>();
        eth_header.dst = MacAddr::from_bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x00]);
        eth_header.src = nic_to_mac(packet.nic);
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
        packet.l2_header.offset = packet.offset as u8;
        packet.offset += packet.l2_header.length as u16;

        Ok(())
    }
}

register_module!(EthSender);
register_module!(EthReceiver);
