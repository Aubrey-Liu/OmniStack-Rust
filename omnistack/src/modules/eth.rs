use omnistack_core::io_module::get_mac_addr;
use omnistack_core::prelude::*;
use omnistack_core::protocols::{EthHeader, EtherType, MacAddr};

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

        // TODO: nic to mac
        let eth_header = packet.get_l2_header::<EthHeader>();
        eth_header.dst = MacAddr::from_bytes([0x3c, 0xfd, 0xfe, 0xbb, 0xc9, 0xc9]);
        eth_header.src = get_mac_addr(packet.nic);
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
        packet.offset += packet.l2_header.length as u16;

        Ok(())
    }
}

register_module!(EthSender);
register_module!(EthReceiver);
