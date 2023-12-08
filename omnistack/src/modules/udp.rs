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
        todo!()
    }
}

impl UdpReceiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for UdpReceiver {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        todo!()
    }
}

register_module!(UdpSender);
register_module!(UdpReceiver);
