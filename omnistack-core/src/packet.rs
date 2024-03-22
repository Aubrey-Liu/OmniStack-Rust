use std::mem::transmute;
use std::ptr::null_mut;
use std::sync::Mutex;

use dpdk_sys as sys;

use crate::memory::{Aligned, MemoryError, MemoryPool};
use crate::service::ThreadId;

pub const MTU: u16 = 1500;
pub const DEFAULT_OFFSET: u32 = 64;
pub const PACKET_BUF_SIZE: usize = MTU as usize + DEFAULT_OFFSET as usize;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct Header {
    pub length: u16,
    pub offset: u16,
}

#[repr(C)]
#[derive(Debug)]
pub enum PktBufType {
    Local,
    Mbuf(*mut sys::rte_mbuf),
}

#[repr(C)]
pub struct Packet {
    // WARNING: field order is crucial for performance
    pub len: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,

    pub offset: u16,

    // pub flow_hash: u32,
    pub data: *mut u8,
    pub next: *mut Packet,

    pub nic: u16,
    pub refcnt: usize,
    pub buf_ty: PktBufType,

    pub buf: Aligned<[u8; PACKET_BUF_SIZE]>,
}

impl Packet {
    pub const fn new() -> Self {
        Self {
            len: DEFAULT_OFFSET as _,
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
            // flow_hash: 0,
            buf: Aligned([0; PACKET_BUF_SIZE]),
        }
    }

    #[inline(always)]
    pub fn len(&self) -> u16 {
        self.len - self.offset
    }

    /// WARN: Set the offset before calling `set_len`
    #[inline(always)]
    pub fn set_len(&mut self, len: u16) {
        self.len = len + self.offset;
    }

    #[inline(always)]
    pub fn data(&self) -> *mut u8 {
        unsafe { self.data.add(self.offset as _) }
    }

    #[inline(always)]
    pub fn iova(&mut self) -> u64 {
        debug_assert!(matches!(self.buf_ty, PktBufType::Local));

        MemoryPool::virt2iova(self as *mut _ as *mut _)
            + (self.data() as u64 - self as *const _ as u64)
    }

    #[inline(always)]
    pub fn parse<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data().cast::<T>() }
    }

    #[inline(always)]
    pub fn get_l2_header<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data.add(self.l2_header.offset as _).cast::<T>() }
    }

    #[inline(always)]
    pub fn get_l3_header<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data.add(self.l3_header.offset as _).cast::<T>() }
    }

    #[inline(always)]
    pub fn get_l4_header<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data.add(self.l4_header.offset as _).cast::<T>() }
    }
}

pub struct PacketPool {
    mp: &'static mut MemoryPool,
    thread_id: u32,
}

impl PacketPool {
    const PACKET_POOL_SIZE: u32 = (1 << 16) - 1;
    const CACHE_SIZE: u32 = 256;

    pub fn get_or_create(socket_id: u32) -> Self {
        let pkt_size = std::mem::size_of::<Packet>();
        let name = format!("pktpool_{socket_id}");

        assert!(ThreadId::is_valid(), "thread id wasn't properly set");

        let thread_id = ThreadId::get();

        let mp = unsafe {
            MemoryPool::get_or_create(&name, Self::PACKET_POOL_SIZE, pkt_size as _, socket_id)
                .unwrap()
        };
        unsafe { mp.create_cache(Self::CACHE_SIZE, thread_id) };

        Self { mp, thread_id }
    }

    #[inline(always)]
    pub fn allocate(&self) -> Result<&'static mut Packet, MemoryError> {
        let mut objs = [null_mut(); 1];

        unsafe { self.mp.allocate(&mut objs, 1, self.thread_id)? };

        Ok(unsafe { &mut *objs.get_unchecked_mut(0).cast::<Packet>() })
    }

    #[inline(always)]
    pub fn allocate_many(&self, n: u32, pkts: &mut [*mut Packet]) -> Result<(), MemoryError> {
        unsafe { self.mp.allocate(transmute(pkts), n, self.thread_id) }
    }

    #[inline(always)]
    pub fn deallocate(packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        if packet.refcnt == 1 {
            packet.refcnt = 0;

            if let PktBufType::Mbuf(m) = packet.buf_ty {
                unsafe { sys::rte_pktmbuf_free(m) };
            }

            unsafe { MemoryPool::deallocate(packet as *mut _ as *mut _, ThreadId::get()) };
        } else if packet.refcnt == 0 {
            log::error!("double free");
        } else {
            packet.refcnt -= 1;
        }
    }
}
