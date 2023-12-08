use omnistack_core::{prelude::*, protocols::Ipv4Header};

struct Ipv4Sender;
struct Ipv4Receiver;

impl Ipv4Sender {
    pub fn new() -> Self {
        Self
    }
}

impl Module for Ipv4Sender {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        todo!()
    }
}

impl Ipv4Receiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for Ipv4Receiver {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        let ipv4 = packet.parse::<Ipv4Header>();
        packet.l3_header.offset = packet.offset as _;
        packet.l3_header.length = ipv4.ihl() << 2;
        packet.length = u16::from_be(ipv4.tot_len);
        packet.offset += packet.l3_header.length as u16;

        if ipv4.ihl() >= 5 && ipv4.ttl > 0 {
            ctx.push_task_downstream(packet);
        }

        Ok(())
    }
}

register_module!(Ipv4Sender);
register_module!(Ipv4Receiver);
