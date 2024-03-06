#![allow(unused)]

use std::{fmt::Debug, net::Ipv4Addr};

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct MacAddr {
    raw: [u8; 6],
}

impl MacAddr {
    pub const fn from_bytes(bytes: [u8; std::mem::size_of::<Self>()]) -> Self {
        Self { raw: bytes }
    }

    pub const fn as_bytes(&self) -> [u8; 6] {
        self.raw
    }
}

impl Debug for MacAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "[{:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x}]",
            self.raw[0], self.raw[1], self.raw[2], self.raw[3], self.raw[4], self.raw[5]
        ))
    }
}

#[repr(u16)]
#[derive(Debug, Clone, Copy)]
pub enum EtherType {
    Ipv4 = 0x0800_u16.to_be(),
    Arp = 0x0806_u16.to_be(),
    Ipv6 = 0x86DD_u16.to_be(),
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct EthHeader {
    pub dst: MacAddr,
    pub src: MacAddr,
    pub ether_ty: EtherType,
}

#[repr(u8)]
#[derive(Debug, Clone, Copy)]
pub enum Ipv4ProtoType {
    TCP = 6,
    UDP = 11,
}

// TODO: design choices (1) mutable reference (2) pointer + dump method
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Ipv4Header {
    version_ihl: u8, /* version:4 | ihl:4 */
    pub tos: u8,
    pub tot_len: u16,
    pub id: u16,
    pub frag_off: u16,
    pub ttl: u8,
    pub protocol: Ipv4ProtoType,
    pub check: u16,
    pub src: u32,
    pub dst: u32,
    // options start here
}

impl Ipv4Header {
    pub fn version(&self) -> u8 {
        self.version_ihl & 0xf
    }

    pub fn ihl(&self) -> u8 {
        self.version_ihl & 0xf0
    }

    pub fn set_version(&mut self, version: u8) {
        debug_assert!(version <= ((1 << 4) - 1));

        self.version_ihl |= version;
    }

    pub fn set_ihl(&mut self, ihl: u8) {
        debug_assert!(ihl <= ((1 << 4) - 1));

        self.version_ihl |= (ihl << 4);
    }
}

pub struct Route {
    ip_addr: u32,

    pub cidr: u8,
    cidr_mask: u32,

    pub port: u16,
}

impl Route {
    pub fn new(ip_addr: u32, cidr: u8, port: u16) -> Self {
        assert!(cidr <= u32::BITS as u8);

        Self {
            ip_addr,
            cidr,
            cidr_mask: (1 << cidr) - 1,
            port,
        }
    }

    pub fn matches(&self, dst_ip_addr: u32) -> bool {
        ((self.ip_addr ^ dst_ip_addr) & self.cidr_mask) == 0
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct UdpHeader {
    pub src: u16,
    pub dst: u16,
    pub len: u16,
    chksum: u16,
}
