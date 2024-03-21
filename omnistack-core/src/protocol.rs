use thiserror::Error;

use std::{fmt::Debug, str::FromStr};

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct MacAddr {
    raw: [u8; 6],
}

impl MacAddr {
    #[inline(always)]
    pub const fn new() -> Self {
        Self::from_bytes([0; 6])
    }

    #[inline(always)]
    pub const fn from_bytes(bytes: [u8; std::mem::size_of::<Self>()]) -> Self {
        Self { raw: bytes }
    }

    #[inline(always)]
    pub const fn as_bytes(&self) -> [u8; 6] {
        self.raw
    }
}

impl Debug for MacAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "[{:#x}:{:#x}:{:#x}:{:#x}:{:#x}:{:#x}]",
            self.raw[0], self.raw[1], self.raw[2], self.raw[3], self.raw[4], self.raw[5]
        ))
    }
}

// TODO: maybe just use std::net
#[repr(transparent)]
pub struct Ipv4Addr {
    octets: [u8; 4],
}

impl Debug for Ipv4Addr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "{}.{}.{}.{}",
            self.octets[0], self.octets[1], self.octets[2], self.octets[3]
        ))
    }
}

impl Ipv4Addr {
    pub const fn new(octets: [u8; 4]) -> Self {
        Self { octets }
    }

    pub const fn from_be(x: u32) -> Self {
        Self::new(x.to_le_bytes())
    }

    #[inline(always)]
    pub fn octets(&self) -> [u8; 4] {
        self.octets
    }

    #[inline(always)]
    pub fn to_u32_le(&self) -> u32 {
        u32::from_be_bytes(self.octets)
    }

    #[inline(always)]
    pub fn to_u32_be(&self) -> u32 {
        u32::from_le_bytes(self.octets)
    }
}

#[derive(Debug, Error)]
pub enum ParseIpv4Error {
    #[error("address is invalid")]
    InvalidAddress,
}

impl FromStr for Ipv4Addr {
    type Err = ParseIpv4Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut octets = s.split('.').map(|octet| octet.parse::<u8>());
        let mut ipv4 = Self::new([0; 4]);
        let mut idx = 0;

        while let Some(Ok(octet)) = octets.next() {
            ipv4.octets[idx] = octet;
            idx += 1;
            if idx > 4 {
                return Err(ParseIpv4Error::InvalidAddress);
            }
        }

        if idx < 4 {
            return Err(ParseIpv4Error::InvalidAddress);
        }

        Ok(ipv4)
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
    TCP = 0x06,
    UDP = 0x11,
}

// TODO: design choices (1) mutable reference (2) pointer + dump method
#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct Ipv4Header {
    version_ihl: u8, /* version:4 | ihl:4 */
    pub tos: u8,
    pub tot_len: u16,
    pub id: u16,
    pub frag_off: u16,
    pub ttl: u8,
    pub protocol: Ipv4ProtoType,
    pub cksum: u16,
    pub src: u32,
    pub dst: u32,
    // options start here
}

impl Debug for Ipv4Header {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Ipv4Header")
            .field("version", &self.version())
            .field("ihl", &self.ihl())
            .field("len", &self.len())
            .field("protocol", &self.protocol)
            .field("src", &Ipv4Addr::from_be(self.src))
            .field("dst", &Ipv4Addr::from_be(self.dst))
            .finish()
    }
}

impl Ipv4Header {
    #[inline(always)]
    pub fn version(&self) -> u8 {
        (self.version_ihl & 0xf0) >> 4
    }

    #[inline(always)]
    pub fn ihl(&self) -> u8 {
        self.version_ihl & 0xf
    }

    #[inline(always)]
    pub fn len(&self) -> u16 {
        u16::from_be(self.tot_len)
    }

    #[inline(always)]
    pub fn set_version(&mut self, version: u8) {
        debug_assert!(version <= ((1 << 4) - 1));

        self.version_ihl |= version;
    }

    #[inline(always)]
    pub fn set_ihl(&mut self, ihl: u8) {
        debug_assert!(ihl <= ((1 << 4) - 1));

        self.version_ihl |= ihl << 4;
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Route {
    ip: u32, // little endian
    cidr_mask: u32,
    pub nic: u16,
    pub cidr: u8,
}

impl Route {
    pub fn new(ip: Ipv4Addr, cidr: u8, nic: u16) -> Self {
        assert!(cidr <= u32::BITS as u8);

        Self {
            ip: ip.to_u32_be(),
            cidr_mask: (1 << cidr) - 1,
            cidr,
            nic,
        }
    }

    // ip is of little endian
    #[inline(always)]
    pub fn matches(&self, ip: u32) -> bool {
        ((self.ip ^ ip) & self.cidr_mask) == 0
    }
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct UdpHeader {
    pub src: u16,
    pub dst: u16,
    pub len: u16,
    pub chksum: u16,
}

impl Debug for UdpHeader {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("UdpHeader")
            .field("src", &u16::from_be(self.src))
            .field("dst", &u16::from_be(self.dst))
            .field("len", &u16::from_be(self.len))
            .finish()
    }
}
