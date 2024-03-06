use std::ffi::*;
use std::marker::PhantomData;
use std::ptr::null_mut;

use omnistack_sys as sys;

pub const MTU: usize = 1500;
pub const DEFAULT_OFFSET: usize = 140;
pub const PACKET_BUF_SIZE: usize = MTU + DEFAULT_OFFSET;

#[derive(Debug, Clone, Copy)]
pub struct Header {
    pub length: u8,
    pub offset: u16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Packet {
    // start address of the un-parsed part
    pub data: *mut u8,
    pub offset: u16,
    pub data_len: u16,

    pub port: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,
    // all fields above is managed by modules

    // set by the allocator initially
    pub refcnt: usize, // TODO: is it really necessary?
    pub buf: [u8; PACKET_BUF_SIZE],
}

impl Packet {
    pub fn init_from_mbuf(&mut self, mbuf: *mut sys::rte_mbuf) {
        let mbuf = unsafe { &mut *mbuf };
        self.data = unsafe { mbuf.buf_addr.add(mbuf.data_off as _) as _ };
        self.offset = mbuf.data_off;
        self.set_len(mbuf.data_len);

        // TODO: flow hash
    }

    pub fn len(&self) -> u16 {
        log::debug!("len: {}", self.data_len - self.offset);

        self.data_len - self.offset
    }

    // NOTE: Set offset before the length
    pub fn set_len(&mut self, len: u16) {
        log::debug!("set len: {}", len + self.offset);

        self.data_len = len + self.offset;
    }

    pub fn data_offset_from_base(&self) -> u16 {
        let base = self as *const Self as *const u8;
        let data = self.data();

        (data as usize - base as usize) as _
    }

    pub fn data(&self) -> *mut u8 {
        self.data.wrapping_add(self.offset as _)
    }

    pub fn parse<T>(&mut self) -> &'static mut T {
        unsafe { &mut *self.data().cast::<T>() }
    }

    pub fn get_l2_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l2_header.offset as _) }
    }

    pub fn get_l3_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l3_header.offset as _) }
    }

    pub fn get_l4_header<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.l4_header.offset as _) }
    }

    unsafe fn get_payload_at<T>(&mut self, offset: usize) -> &'static mut T {
        &mut *self.data.add(offset).cast::<T>()
    }
}

// TODO: Rewrite PacketPool

// TODO: Need a packet pool and a memory pool.
// 1. memory pool args: cpu, size, cnt (one packet pool per numa node)
// 2. packet pool is one layer above mpool and only allocates `Packet`
#[repr(C)]
#[derive(Debug)]
pub struct PacketPool {
    mp: MemoryPool<Packet>,
}

const MEMPOOL_CACHE_SIZE: usize = 250;

impl PacketPool {
    const PACKET_POOL_SIZE: u32 = (1 << 16) - 1;

    pub const fn empty() -> Self {
        Self {
            mp: MemoryPool::empty(),
        }
    }

    pub fn get_or_create(cpu: i32) -> Self {
        let socket_id = match unsafe { sys::numa_node_of_cpu(cpu) } {
            x @ 0.. => x,
            _ => 0,
        };

        let pkt_size = std::mem::size_of::<Packet>();
        let elt_size = (pkt_size + 64 - 1) / 64 * 64;

        Self {
            mp: MemoryPool::get_or_create(
                "pktpool",
                Self::PACKET_POOL_SIZE as _,
                elt_size as _,
                socket_id,
            ),
        }
    }

    pub fn allocate(&self, thread_id: u16) -> *mut Packet {
        self.mp.get(thread_id)
    }

    pub fn deallocate(&self, packet: *mut Packet, thread_id: u16) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            self.mp.put(packet, thread_id);
        }
    }
}

const MAX_THREADS: usize = 32;

#[derive(Debug)]
pub struct MemoryPool<T> {
    // Maybe useful if I want to write my own memory pool
    elt_size: usize,
    n: usize,
    socket_id: i32,
    pool: *mut sys::rte_mempool,
    cache: [*mut sys::rte_mempool_cache; MAX_THREADS], // thread id -> cache
    phantom: PhantomData<T>,
}

impl<T> MemoryPool<T> {
    pub const fn empty() -> Self {
        Self {
            elt_size: 0,
            n: 0,
            socket_id: 0,

            pool: std::ptr::null_mut(),
            cache: [std::ptr::null_mut(); MAX_THREADS],
            phantom: PhantomData,
        }
    }

    // NOTE: cpu is specified by user in config
    pub fn get_or_create(name: &str, n: usize, elt_size: usize, socket_id: i32) -> Self {
        assert!(MAX_THREADS * MEMPOOL_CACHE_SIZE < n);

        let mut mp = Self::empty();

        // TODO: use rte_* functions instead of node_of_cpu?
        let name = CString::new(format!("{name}-{socket_id}")).unwrap();

        mp.pool = unsafe { sys::rte_mempool_lookup(name.as_ptr()) };
        if mp.pool.is_null() {
            mp.pool = unsafe {
                sys::rte_mempool_create(
                    name.as_ptr(),
                    n as _,
                    elt_size as _,
                    0,
                    0,
                    None,
                    null_mut(),
                    None,
                    null_mut(),
                    socket_id as _,
                    0,
                )
            };
        }

        if mp.pool.is_null() {
            panic!("failed to create a memory pool.");
        }

        // User cache
        for cache in &mut mp.cache {
            let c = unsafe { sys::rte_mempool_cache_create(MEMPOOL_CACHE_SIZE as _, socket_id) };
            if c.is_null() {
                panic!("failed to create memory pool cache");
            }
            *cache = c;
        }

        mp.elt_size = elt_size;
        mp.n = n;
        mp.socket_id = socket_id;

        mp
    }

    // NOTE: thread_id came from ctx
    // TODO: get many
    pub fn get(&self, thread_id: u16) -> *mut T {
        let mut objs = [null_mut::<T>(); 1];

        let r = unsafe {
            sys::rte_mempool_generic_get(
                self.pool,
                objs.as_mut_ptr() as _,
                1,
                self.cache[thread_id as usize],
            )
        };

        if r != 0 {
            log::error!("Memory exhausted");
        }

        objs[0]
    }

    pub fn put(&self, obj: *mut T, thread_id: u16) {
        let obj = &obj as *const _ as *const _;
        unsafe { sys::rte_mempool_generic_put(self.pool, obj, 1, self.cache[thread_id as usize]) };
    }
}

impl<T> Drop for MemoryPool<T> {
    fn drop(&mut self) {
        log::debug!("Dropping MemoryPool");

        unsafe { sys::rte_mempool_free(self.pool) };
    }
}
