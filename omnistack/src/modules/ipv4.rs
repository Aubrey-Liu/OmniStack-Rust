use std::{net::Ipv4Addr, str::FromStr};

use omnistack_core::prelude::*;

struct Ipv4Sender {
    route_table: Vec<Route>,
}

struct Ipv4Receiver;

impl Ipv4Sender {
    pub fn new() -> Self {
        let ip = u32::from_ne_bytes(Ipv4Addr::from_str("192.168.10.0").unwrap().octets());

        // TODO: read from the config
        Self {
            route_table: vec![Route::new(ip, 24, 0)],
        }
    }
}

impl Module for Ipv4Sender {
    fn process(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        // TODO: get them from the user
        let src_addr = u32::from_ne_bytes(Ipv4Addr::from_str("192.168.10.1").unwrap().octets());
        let dst_addr = u32::from_ne_bytes(Ipv4Addr::from_str("192.168.10.2").unwrap().octets());

        let mut max_cidr = 0;
        let mut dst_port = None;

        self.route_table.iter().for_each(|route| {
            if route.matches(dst_addr) && route.cidr > max_cidr {
                max_cidr = route.cidr;
                dst_port = Some(route.port);
            }
        });

        if dst_port.is_none() {
            return Err(Error::InvalidDest);
        }

        packet.port = dst_port.unwrap();

        packet.offset -= std::mem::size_of::<Ipv4Header>() as u16;
        packet.l3_header.length = std::mem::size_of::<Ipv4Header>() as u8;
        packet.l3_header.offset = packet.offset;

        let ipv4_hdr = packet.get_l3_header::<Ipv4Header>();
        ipv4_hdr.set_version(4);
        ipv4_hdr.set_ihl(packet.l3_header.length >> 2);
        ipv4_hdr.tot_len = packet.len().to_be();
        ipv4_hdr.protocol = Ipv4ProtoType::UDP; // TODO: include tcp
        ipv4_hdr.ttl = 255;
        ipv4_hdr.src = src_addr; // ip address is of big endian already
        ipv4_hdr.dst = dst_addr;
        ipv4_hdr.tos = 0;
        ipv4_hdr.id = 0;
        ipv4_hdr.frag_off = 0;

        ctx.push_task_downstream(packet);

        Ok(())
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
        packet.set_len(u16::from_be(ipv4.tot_len));
        packet.offset += packet.l3_header.length as u16;

        if ipv4.ihl() >= 5 && ipv4.ttl > 0 {
            ctx.push_task_downstream(packet);
        }

        Ok(())
    }
}

register_module!(Ipv4Sender);
register_module!(Ipv4Receiver);
