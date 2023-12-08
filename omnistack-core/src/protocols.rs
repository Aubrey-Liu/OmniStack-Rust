#![allow(unused)]

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct MacAddr {
    raw: [u8; 6],
}

#[repr(u16)]
#[derive(Debug, Clone, Copy)]
pub enum EtherType {
    Ipv4 = 0x0800,
    Arp = 0x0806,
    Ipv6 = 0x86DD,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct EthHeader {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub ether_ty: EtherType,
}

impl From<&mut [u8]> for &mut EthHeader {
    fn from(value: &mut [u8]) -> Self {
        debug_assert!(value.len() == std::mem::size_of::<EthHeader>());

        unsafe { std::mem::transmute(value.as_mut_ptr()) }
    }
}

// todo: design choices (1) mutable reference (2) pointer + dump method

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Ipv4Header {
    version_ihl: u8, /* version:4 | ihl:4 */
    pub tos: u8,
    pub tot_len: u16,
    pub id: u16,
    pub frag_off: u16,
    pub ttl: u8,
    pub protocol: u8,
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
        debug_assert!(version <= (1 << 4 - 1));

        self.version_ihl |= version;
    }

    pub fn set_ihl(&mut self, ihl: u8) {
        debug_assert!(ihl <= (1 << 4 - 1));

        self.version_ihl |= (ihl << 4);
    }
}
