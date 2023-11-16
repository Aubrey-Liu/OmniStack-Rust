use std::sync::Mutex;

use crate::memory::MemoryPool;

pub type PacketId = usize;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Packet {
    pub buf: [u8; PACKET_BUF_SIZE],
    pub refcnt: usize,
}

pub const PACKET_BUF_SIZE: usize = 1500;
const PACKET_POOL_CAP: usize = 65536;

pub struct PacketPool(Mutex<MemoryPool<Packet>>);

unsafe impl Send for PacketPool {}

// todo: is lock necessary? faster mutex?

impl PacketPool {
    fn get() -> &'static PacketPool {
        static PACKET_POOL: PacketPool = PacketPool(Mutex::new(MemoryPool::empty()));

        &PACKET_POOL
    }

    pub fn init(core_id: i32) {
        Self::get().0.lock().unwrap().init(PACKET_POOL_CAP, core_id);
    }

    pub fn allocate() -> Option<*mut Packet> {
        Self::get().0.lock().unwrap().allocate().map(|packet| {
            unsafe { packet.as_mut().unwrap().refcnt = 1 };
            packet
        })
    }

    pub fn deallocate(packet: *mut Packet) {
        let packet = unsafe { packet.as_mut().unwrap() };
        debug_assert!(packet.refcnt != 0, "doubly free a packet");

        packet.refcnt -= 1;
        if packet.refcnt == 0 {
            Self::get().0.lock().unwrap().deallocate(packet as *mut _);
            println!("a packet is freed");
        }
    }
}
