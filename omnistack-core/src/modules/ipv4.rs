use std::mem::size_of;

use crate::{config::ConfigManager, prelude::*};
use arrayvec::ArrayVec;

#[derive(omnistack_proc::Module)]
struct Ipv4Sender {
    route_table: ArrayVec<Route, 10>,
}

#[derive(omnistack_proc::Module)]
struct Ipv4Receiver;

impl Ipv4Sender {
    pub fn new() -> Self {
        Self {
            route_table: ArrayVec::new(),
        }
    }
}

impl Module for Ipv4Sender {
    fn init(&mut self, ctx: &Context) -> Result<()> {
        let config = ConfigManager::get();
        let stack_config = config.get_stack_config(ctx.stack_name).unwrap();
        self.route_table
            .try_extend_from_slice(&stack_config.routes)
            .unwrap();

        Ok(())
    }
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        // TODO: read from user information, which will be binded with the packet
        const SRC: u32 = u32::from_le_bytes([192, 168, 10, 1]);
        const DST: u32 = u32::from_le_bytes([192, 168, 10, 2]);

        let mut max_cidr = 0;
        let mut dst_nic = None;

        for route in &self.route_table {
            if route.matches(DST) && route.cidr > max_cidr {
                max_cidr = route.cidr;
                dst_nic = Some(route.nic);
            }
        }

        packet.nic = dst_nic.unwrap_or(0);
        packet.offset -= size_of::<Ipv4Header>() as u16;
        packet.l3_header.length = size_of::<Ipv4Header>() as u16;
        packet.l3_header.offset = packet.offset;

        let ipv4_hdr = packet.get_l3_header::<Ipv4Header>();
        ipv4_hdr.set_version(4);
        ipv4_hdr.set_ihl(size_of::<Ipv4Header>() as u8 >> 2);
        ipv4_hdr.tot_len = packet.len().to_be();
        ipv4_hdr.protocol = Ipv4ProtoType::UDP;
        ipv4_hdr.ttl = 255;
        ipv4_hdr.src = SRC; // ip address is of big endian already
        ipv4_hdr.dst = DST;
        ipv4_hdr.tos = 0;
        ipv4_hdr.id = 0;
        ipv4_hdr.frag_off = 0;

        Ok(())
    }
}

impl Ipv4Receiver {
    pub fn new() -> Self {
        Self
    }
}

impl Module for Ipv4Receiver {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        let ipv4 = packet.parse::<Ipv4Header>();

        packet.l3_header.offset = packet.offset as _;
        packet.l3_header.length = (ipv4.ihl() << 2) as u16;
        packet.set_len(ipv4.len());
        packet.offset += packet.l3_header.length;

        if ipv4.ihl() < 5 || ipv4.ttl == 0 {
            PacketPool::deallocate(packet);

            Err(Error::Dropped)
        } else {
            Ok(())
        }
    }
}
