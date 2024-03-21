use crate::prelude::*;

#[derive(omnistack_proc::Module)]
struct UdpSender;

#[derive(omnistack_proc::Module)]
struct UdpReceiver;

impl UdpSender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for UdpSender {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.offset -= std::mem::size_of::<UdpHeader>() as u16;
        packet.l4_header.length = std::mem::size_of::<UdpHeader>() as u16;
        packet.l4_header.offset = packet.offset;

        let udp_hdr = packet.get_l4_header::<UdpHeader>();
        udp_hdr.src = 80_u16.to_be(); // TODO: read from user
        udp_hdr.dst = 81_u16.to_be();
        udp_hdr.len = packet.len().to_be();

        Ok(())
    }
}

impl UdpReceiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for UdpReceiver {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l4_header.length = std::mem::size_of::<UdpHeader>() as u16;
        packet.l4_header.offset = packet.offset;
        packet.offset += std::mem::size_of::<UdpHeader>() as u16;

        Ok(())
    }
}
