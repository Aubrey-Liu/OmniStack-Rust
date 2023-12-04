use omnistack_core::prelude::*;

#[allow(unused)]
#[repr(u16)]
pub enum EtherType {
    Ipv4 = 0x0800,
    Arp = 0x0806,
    Ipv6 = 0x86DD,
}

#[repr(C, packed)]
pub struct EthHeader {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub ether_ty: EtherType,
}

struct EthSender;
struct EthReceiver;

impl EthSender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for EthSender {
    // todo: nic to mac (maybe hardcode at first)
    #[allow(unused)]
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        todo!()
    }
}

impl EthReceiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for EthReceiver {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l2_header.length = std::mem::size_of::<EthHeader>() as _;
        packet.l2_header.offset = packet.offset as u8;
        packet.offset += packet.l2_header.length as u16;

        ctx.push_task_downstream(packet);

        Ok(())
    }
}

register_module!(EthSender);
register_module!(EthReceiver);
