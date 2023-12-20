use omnistack_core::prelude::*;

struct UdpSender;
struct UdpReceiver;

impl UdpSender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for UdpSender {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.offset -= std::mem::size_of::<UdpHeader>() as u16;
        packet.l4_header.length = std::mem::size_of::<UdpHeader>() as u8;
        packet.l4_header.offset = packet.offset;

        let udp_hdr = packet.get_l4_header::<UdpHeader>();
        udp_hdr.src = 80_u16.to_be(); // todo
        udp_hdr.dst = 81_u16.to_be();
        udp_hdr.len = packet.len().to_be();

        log::debug!("Send L4 header {:?}", packet.get_l4_header::<UdpHeader>());

        ctx.push_task_downstream(packet);

        Ok(())
    }
}

impl UdpReceiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for UdpReceiver {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        packet.l4_header.length = std::mem::size_of::<UdpHeader>() as u8;
        packet.l4_header.offset = packet.offset;
        packet.offset += std::mem::size_of::<UdpHeader>() as u16;

        ctx.push_task_downstream(packet);

        log::debug!("Receive L4 header {:?}", packet.get_l4_header::<UdpHeader>());

        Ok(())
    }
}

register_module!(UdpSender);
register_module!(UdpReceiver);
