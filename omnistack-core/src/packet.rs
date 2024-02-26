use std::{ffi::*, marker::PhantomData};

use omnistack_sys::dpdk as sys;

pub type PacketId = usize;

pub const MTU: usize = 1500;
// TODO: Find better defaults
pub const DEFAULT_OFFSET: usize = 140;
pub const PACKET_BUF_SIZE: usize = MTU + DEFAULT_OFFSET;

#[derive(Debug, Clone, Copy)]
pub struct Header {
    pub length: u8,
    pub offset: u16,
}

#[derive(Debug, Clone, Copy)]
#[repr(transparent)]
pub struct Mbuf(pub *mut sys::rte_mbuf);

impl Mbuf {
    pub fn inner(&self) -> *mut sys::rte_mbuf {
        self.0
    }

    pub fn data_addr(&self) -> *mut u8 {
        let inner = self.as_mut();
        unsafe { inner.buf_addr.add(inner.data_off as _).cast() }
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

// stored in the `metapool` of PacketPool
// TODO: Redesign the Packet structure to "metadata plus a buffer"
// 1. If recv from user, data points to the internal buffer
// 2. If recv from device, data points to external mbuf
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Packet {
    // start address of the un-parsed part
    pub data: *mut u8,
    pub offset: u16,
    length: u16,

    pub port: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,
    // all fields above is managed by modules

    // set by the allocator initially
    pub refcnt: usize, // TODO: is it really necessary?
    pub mbuf: [u8; PACKET_BUF_SIZE],
}

impl Packet {
    pub fn init_from_mbuf(&mut self, mbuf: Mbuf) {
        self.data = mbuf.data_addr();
    }

    pub fn len(&self) -> u16 {
        // TODO: make sure it is correct
        self.length - self.offset
    }

    /// Pls set the offset before the length
    pub fn set_len(&mut self, len: u16) {
        self.length = len + self.offset;
    }

    pub fn parse<T>(&mut self) -> &'static mut T {
        unsafe { self.get_payload_at::<T>(self.offset as _) }
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

// TODO: Need a packet pool and a memory pool.
// 1. memory pool args: cpu, size, cnt (one packet pool per numa node)
// 2. packet pool is one layer above mpool and only allocates `Packet`
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct PacketPool {
    pub pktpool: MemoryPool<Packet>,
    pub pktmbuf: *mut sys::rte_mempool,
}

const MEMPOOL_SIZE: usize = 4095;
const MEMPOOL_CACHE_SIZE: usize = 250;

impl PacketPool {
    pub const fn empty() -> Self {
        Self {
            pktpool: MemoryPool::empty(),
            pktmbuf: std::ptr::null_mut(),
        }
    }

    pub fn get_or_create(cpu: i32) -> Self {
        let socket_id = match unsafe { sys::node_of_cpu(cpu) } {
            x @ 0.. => x,
            _ => 0,
        };

        let pktmbuf_pool_name = CString::new(format!("omnistack-pktmbuf-{socket_id}")).unwrap();

        Self {
            // TODO: socket id
            pktpool: MemoryPool::get_or_create(
                "pktpool",
                std::mem::size_of::<Packet>() as _,
                MEMPOOL_SIZE as _,
                cpu,
            ),
            pktmbuf: unsafe {
                sys::pktpool_create(
                    pktmbuf_pool_name.as_ptr(),
                    MEMPOOL_SIZE as _,
                    MEMPOOL_CACHE_SIZE as _,
                    socket_id as _,
                )
            },
        }
    }

    pub fn allocate_mbuf(&self) -> Option<*mut sys::rte_mbuf> {
        let mbuf = unsafe { sys::pktpool_alloc(self.pktmbuf) };
        if mbuf.is_null() {
            None
        } else {
            Some(mbuf)
        }
    }

    pub fn allocate_pkt(&self) -> Option<&'static mut Packet> {
        let packet = unsafe { &mut *self.pktpool.get().unwrap() };
        packet.refcnt = 1;

        Some(packet)
    }

    pub fn deallocate_pkt(&self, packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            self.pktpool.put(packet);
        }
    }
}

unsafe impl Send for PacketPool {}

/* NOTE: the mempool implementation is not preemptible. An lcore must not be interrupted by another
 * task that uses the same mempool (because it uses a ring which is not preemptible). Also, usual mempool
 * functions like rte_mempool_get() or rte_mempool_put() are designed to be called from an EAL thread due
 * to the internal per-lcore cache. Due to the lack of caching, rte_mempool_get() or rte_mempool_put()
 * performance will suffer when called by unregistered non-EAL threads. Instead, unregistered non-EAL threads
 * should call rte_mempool_generic_get() or rte_mempool_generic_put() with a user cache created with rte_mempool_cache_create().
*/

// TODO: per-core cache
#[derive(Debug, Clone, Copy)]
pub struct MemoryPool<T> {
    pool: *mut sys::rte_mempool,
    phantom: PhantomData<T>,
}

impl<T> MemoryPool<T> {
    pub const fn empty() -> Self {
        Self {
            pool: std::ptr::null_mut(),
            phantom: PhantomData,
        }
    }

    pub fn get_or_create(name: &str, elt_size: u32, n: u32, cpu: i32) -> Self {
        // TODO: use rte_* functions instead of node_of_cpu?
        let socket_id = match unsafe { sys::node_of_cpu(cpu) } {
            x @ 0.. => x,
            _ => 0,
        };

        let name = CString::new(format!("{name}-{socket_id}")).unwrap();

        Self {
            pool: unsafe {
                // get or create
                sys::mempool_create(name.as_ptr(), n, elt_size, 0, socket_id as _)
            },
            phantom: PhantomData, // TODO: create cache for each threads (don't know exact number, so just set a cap)
        }
    }

    pub fn get(&self) -> Option<*mut T> {
        let item = unsafe { sys::mempool_get(self.pool) };
        if item.is_null() {
            None
        } else {
            Some(item as *mut _)
        }
    }

    pub fn put(&self, obj: *mut T) {
        unsafe { sys::mempool_put(self.pool, obj as *mut _) };
    }
}
