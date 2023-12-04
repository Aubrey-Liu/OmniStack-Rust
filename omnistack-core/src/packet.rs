use std::ffi::*;

use omnistack_sys::dpdk as sys;

use crate::headers::EthHeader;

pub type PacketId = usize;

pub const MTU: usize = 1500;
// todo: ???
pub const DEFAULT_OFFSET: usize = 140;
pub const PACKET_BUF_SIZE: usize = MTU + std::mem::size_of::<EthHeader>();

#[derive(Debug, Clone, Copy)]
pub struct Header {
    pub length: u8,
    pub offset: u8,
}

#[repr(transparent)]
pub struct Mbuf(*mut sys::rte_mbuf);

impl Mbuf {
    pub fn inner(&self) -> *mut sys::rte_mbuf {
        self.0
    }

    pub fn inc_data_off(&self, n: u16) {
        self.as_mut().data_off += n;
    }

    pub fn set_data_len(&self, len: u16) {
        self.as_mut().data_len = len;
    }

    pub fn set_pkt_len(&self, len: u32) {
        self.as_mut().pkt_len = len;
    }

    pub fn set_l2_len(&self, len: u8) {
        unsafe {
            self.as_mut()
                .__bindgen_anon_3
                .__bindgen_anon_1
                .set_l2_len(len as _)
        };
    }

    pub fn set_l3_len(&self, len: u8) {
        unsafe {
            self.as_mut()
                .__bindgen_anon_3
                .__bindgen_anon_1
                .set_l3_len(len as _)
        };
    }

    pub fn set_l4_len(&self, len: u8) {
        unsafe {
            self.as_mut()
                .__bindgen_anon_3
                .__bindgen_anon_1
                .set_l4_len(len as _)
        };
    }

    fn as_mut(&self) -> &mut sys::rte_mbuf {
        unsafe { self.0.as_mut().unwrap() }
    }
}

#[repr(C)]
pub struct Packet {
    pub refcnt: usize,

    pub mbuf: Mbuf,

    pub nic: u16,

    // start address of the un-parsed part
    pub offset: u16,
    pub length: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,

    pub data: [u8; PACKET_BUF_SIZE],
}

impl Packet {
    pub fn from_mbuf(mbuf: *mut sys::rte_mbuf) -> &'static mut Packet {
        let mbuf = unsafe { &mut *mbuf };

        let pkt = unsafe {
            mbuf.buf_addr
                .add(mbuf.data_off as _)
                .cast::<Self>()
                .as_mut()
                .unwrap()
        };

        pkt.offset = DEFAULT_OFFSET as _;
        pkt.refcnt = 1;
        pkt.nic = 0;
        pkt.mbuf = Mbuf(mbuf);
        pkt.length = 1200; // todo

        pkt
    }

    pub fn len(&self) -> u16 {
        self.length
    }

    pub fn data_offset(&self) -> u16 {
        // ## mbuf ## headroom ## meta ## packet data
        self.offset + Self::extra_size()
    }

    fn extra_size() -> u16 {
        (std::mem::size_of::<Self>() - PACKET_BUF_SIZE) as _
    }

    pub fn get_l2_header<'a, T>(&mut self) -> &'a mut T
    where
        &'a mut T: From<&'a mut [u8]>,
    {
        unsafe {
            std::slice::from_raw_parts_mut(
                self.data.as_mut_ptr().add(self.l2_header.offset as _),
                self.l2_header.length as _,
            )
            .into()
        }
    }

    pub fn get_l3_header<'a, T>(&mut self) -> &'a mut T
    where
        &'a mut T: From<&'a mut [u8]>,
    {
        unsafe {
            std::slice::from_raw_parts_mut(
                self.data.as_mut_ptr().add(self.l3_header.offset as _),
                self.l3_header.length as _,
            )
            .into()
        }
    }

    pub fn get_l4_header<'a, T>(&mut self) -> &'a mut T
    where
        &'a mut T: From<&'a mut [u8]>,
    {
        unsafe {
            std::slice::from_raw_parts_mut(
                self.data.as_mut_ptr().add(self.l4_header.offset as _),
                self.l4_header.length as _,
            )
            .into()
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct PacketPool {
    pub mempool: *mut sys::rte_mempool,
}

// todo: is lock necessary? faster mutex?

impl PacketPool {
    pub fn get_or_create(name: &str) -> Self {
        let name = CString::new(format!("omnistack-{name}")).unwrap();

        Self {
            mempool: unsafe { sys::pktpool_create(name.as_ptr()) },
        }
    }

    pub fn allocate(&self) -> Option<&mut Packet> {
        let mbuf = unsafe { sys::pktpool_alloc(self.mempool) };

        if mbuf.is_null() {
            return None;
        }

        Some(Packet::from_mbuf(mbuf))
    }

    pub fn deallocate(&self, packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            unsafe { sys::pktpool_dealloc(packet.mbuf.inner()) };
        }
    }
}
