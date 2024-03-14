use std::ptr::{addr_of, null_mut};

use omnistack_sys as sys;

use crate::memory::MemoryPool;

pub const MTU: u16 = 1500;
pub const DEFAULT_OFFSET: u32 = 128;
pub const PACKET_BUF_SIZE: u32 = MTU as u32 + DEFAULT_OFFSET;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct Header {
    pub length: u8,
    pub offset: u8,
}

#[repr(C)]
#[derive(Debug)]
pub enum PktBufType {
    Local,
    Mbuf(*mut sys::rte_mbuf),
}

#[repr(C)]
#[derive(Debug)]
pub struct Packet {
    pub data_len: u16,

    pub nic: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,

    pub offset: u16,
    pub data: *mut u8,
    pub next: *mut Packet,

    pub buf_ty: PktBufType,

    pub refcnt: u32,
    pub flow_hash: u32,

    padding: [u8; 8],

    pub buf: [u8; PACKET_BUF_SIZE as usize],
}

impl Packet {
    const BUF_OFFSET: usize = {
        let p = Self::new();
        unsafe { p.buf.as_ptr().offset_from(addr_of!(p).cast()) as _ }
    };

    pub const fn new() -> Self {
        Self {
            data_len: DEFAULT_OFFSET as _,
            nic: 0,
            l2_header: Header {
                length: 0,
                offset: 0,
            },
            l3_header: Header {
                length: 0,
                offset: 0,
            },
            l4_header: Header {
                length: 0,
                offset: 0,
            },
            offset: DEFAULT_OFFSET as _,
            data: null_mut(),
            next: null_mut(),
            buf_ty: PktBufType::Local,
            refcnt: 1,
            flow_hash: 0,

            padding: [0; 8],

            buf: [0; PACKET_BUF_SIZE as usize],
        }
    }

    #[inline(always)]
    pub fn len(&self) -> u16 {
        self.data_len - self.offset
    }

    /// WARN: Set the offset before calling `set_len`
    #[inline(always)]
    pub fn set_len(&mut self, len: u16) {
        self.data_len = len + self.offset;
    }

    #[inline(always)]
    pub fn data(&self) -> *mut u8 {
        unsafe { self.data.add(self.offset as _) }
    }

    #[inline(always)]
    pub fn parse<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data().cast::<T>() }
    }

    #[inline(always)]
    pub fn get_l2_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l2_header.offset as _) }
    }

    #[inline(always)]
    pub fn get_l3_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l3_header.offset as _) }
    }

    #[inline(always)]
    pub fn get_l4_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l4_header.offset as _) }
    }

    #[inline(always)]
    unsafe fn get_payload_at<T>(&mut self, offset: usize) -> &'static mut T {
        &mut *self.data.add(offset).cast::<T>()
    }

    // TODO: if buf type is dpdk, can't do this
    #[inline(always)]
    pub fn iova(&mut self) -> u64 {
        MemoryPool::virt2iova(self as *mut _ as *mut _)
            + (self.data() as u64 - self as *const _ as u64)
    }
}

#[derive(Debug)]
pub struct PacketPool {
    pub(crate) mp: MemoryPool,
}

impl PacketPool {
    // TODO: Decide pool size based on how many cpus share this pool.
    const PACKET_POOL_SIZE: u32 = (1 << 16) - 1;
    const PERCORE_CACHE_SIZE: u32 = sys::RTE_MEMPOOL_CACHE_MAX_SIZE;

    pub fn get_or_create(socket_id: i32) -> Self {
        assert_eq!(Packet::BUF_OFFSET, 64);

        let pkt_size = std::mem::size_of::<Packet>();
        let elt_size = (pkt_size + 64 - 1) / 64 * 64;
        let name = format!("pktpool_{socket_id}");

        Self {
            mp: MemoryPool::get_or_create(
                &name,
                Self::PACKET_POOL_SIZE as _,
                elt_size as _,
                Self::PERCORE_CACHE_SIZE,
                socket_id,
            ),
        }
    }

    #[inline(always)]
    pub fn allocate(&self) -> Option<&'static mut Packet> {
        unsafe { self.mp.allocate().cast::<Packet>().as_mut() }
    }

    #[inline(always)]
    pub fn allocate_many(&self, n: u32, pkts: &mut [*mut Packet]) -> i32 {
        self.mp.allocate_many(n, pkts.as_mut_ptr().cast())
    }

    #[inline(always)]
    pub fn deallocate(packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        if packet.refcnt == 1 {
            packet.refcnt = 0;

            if let PktBufType::Mbuf(m) = packet.buf_ty {
                unsafe { sys::rte_pktmbuf_free(m) };
            }

            MemoryPool::deallocate(packet as *mut _ as *mut _);
        } else if packet.refcnt == 0 {
            log::error!("double free");
        } else {
            packet.refcnt -= 1;
        }
    }
}
