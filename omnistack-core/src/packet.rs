use std::ffi::*;

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
pub struct Mbuf(*mut sys::rte_mbuf);

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
#[repr(C)]
#[derive(Debug)]
pub struct Packet {
    // start address of the un-parsed part
    pub offset: u16,
    length: u16,

    pub port: u16,

    pub l2_header: Header,
    pub l3_header: Header,
    pub l4_header: Header,
    // all fields above is managed by modules

    // set by the allocator initially
    pub refcnt: usize, // TODO: is it really necessary?
    pub mbuf: Mbuf,
    pub data: *mut u8,
}

impl Packet {
    pub fn init_from_mbuf(&mut self, mbuf: *mut sys::rte_mbuf) {
        self.mbuf = Mbuf(mbuf);
        self.data = self.mbuf.data_addr();
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

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct PacketPool {
    pub pktinfo: *mut sys::rte_mempool,
    pub pktmbuf: *mut sys::rte_mempool,
}

const MEMPOOL_SIZE: usize = 4095;
const MEMPOOL_CACHE_SIZE: usize = 250;

impl PacketPool {
    pub const fn empty() -> Self {
        Self {
            pktinfo: std::ptr::null_mut(),
            pktmbuf: std::ptr::null_mut(),
        }
    }

    pub fn get_or_create(cpu: i32) -> Self {
        let socket_id = match unsafe { sys::node_of_cpu(cpu) } {
            x @ 0.. => x,
            _ => 0,
        };

        let pktinfo_pool_name = CString::new(format!("omnistack-pktinfo-{socket_id}")).unwrap();
        let pktmbuf_pool_name = CString::new(format!("omnistack-pktmbuf-{socket_id}")).unwrap();

        Self {
            // TODO: socket id
            pktinfo: unsafe {
                sys::mempool_create(
                    pktinfo_pool_name.as_ptr(),
                    MEMPOOL_SIZE as _,
                    std::mem::size_of::<Packet>() as _,
                    MEMPOOL_CACHE_SIZE as _,
                    socket_id as _,
                )
            },
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

    pub fn allocate_meta(&self) -> Option<&'static mut Packet> {
        let packet = unsafe { &mut *sys::mempool_get(self.pktinfo).cast::<Packet>() };
        packet.refcnt = 1;

        Some(packet)
    }

    pub fn allocate(&self) -> Option<&'static mut Packet> {
        let mbuf = unsafe { sys::pktpool_alloc(self.pktmbuf) };
        if mbuf.is_null() {
            return None;
        }

        let packet = unsafe { sys::mempool_get(self.pktinfo) };
        if packet.is_null() {
            unsafe { sys::pktpool_dealloc(mbuf) };
            return None;
        }

        let packet = unsafe { &mut *packet.cast::<Packet>() };
        packet.refcnt = 1;
        packet.init_from_mbuf(mbuf);

        Some(packet)
    }

    pub fn deallocate_meta(&self, packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            unsafe { sys::mempool_put(self.pktinfo, packet as *mut _ as *mut _) };
        }
    }

    pub fn deallocate(&self, packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            unsafe { sys::mempool_put(self.pktinfo, packet as *mut _ as *mut _) };
            unsafe { sys::pktpool_dealloc(packet.mbuf.inner()) };
        }
    }
}

unsafe impl Send for PacketPool {}
