use std::ffi::*;

use omnistack_sys::dpdk::packet as sys;

pub type PacketId = usize;

pub const PACKET_BUF_SIZE: usize = 1500;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Packet {
    pub buf: [u8; PACKET_BUF_SIZE],
    pub refcnt: usize,
    mbuf: *mut sys::rte_mbuf
}

#[repr(C)]
pub struct PacketPool {
    mempool: *mut sys::rte_mempool
}

// todo: is lock necessary? faster mutex?

impl PacketPool {
    pub fn get_or_create(name: &str) -> Self {
        let name = CString::new(format!("omnistack-{name}")).unwrap();

        Self {
            mempool: unsafe { sys::pktpool_create(name.as_ptr()) },
        }
    }

    pub fn allocate(&self) -> Option<&'static mut Packet> {
        let raw_pkt = unsafe {
            sys::pktpool_alloc(self.mempool)
        };

        if raw_pkt.m.is_null() {
            return None;
        }

        let pkt = unsafe { &mut *(raw_pkt.data.cast::<Packet>()) };
        pkt.mbuf = raw_pkt.m;
        pkt.refcnt = 1;

        Some(pkt)
    }

    pub fn deallocate(&self, packet: *mut Packet) {
        let packet = unsafe { &mut *packet };

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            unsafe { sys::pktpool_dealloc(packet.mbuf) };
            println!("a packet is freed");
        }
    }
}
