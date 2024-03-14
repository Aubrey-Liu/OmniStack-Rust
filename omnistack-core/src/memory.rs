use std::ffi::CString;
use std::ptr::null_mut;
use std::sync::Mutex;

use omnistack_sys as sys;

#[derive(Debug)]
pub struct MemoryPool {
    pub(crate) mp: *mut sys::rte_mempool,
    pub(crate) socket_id: i32,
    pub(crate) name: String,
}

// struct Header {
//     mp: *mut sys::rte_mempool,
//     iova: usize,
// }
//
// unsafe extern "C" fn obj_header_init(
//     mp: *mut sys::rte_mempool,
//     _opaque: *mut c_void,
//     obj: *mut c_void,
//     _obj_idx: u32,
// ) {
//     let header = &mut *obj.cast::<Header>();
//     header.mp = mp;
//     header.iova = sys::rte_mempool_virt2iova(obj) as usize + HEADER_ROOM_SIZE;
// }

// const HEADER_ROOM_SIZE: usize = (size_of::<Header>() + 64 - 1) / 64 * 64;

impl MemoryPool {
    pub const fn new() -> Self {
        Self {
            mp: std::ptr::null_mut(),
            socket_id: 0,
            name: String::new(),
        }
    }

    // NOTE: cpu is specified by user in config
    pub fn get_or_create(
        name: &str,
        n: u32,
        elt_size: u32,
        cache_size: u32,
        socket_id: i32,
    ) -> Self {
        let mut pool = Self::new();

        let c_name = CString::new(name).unwrap();
        let mp = unsafe { sys::rte_mempool_lookup(c_name.as_ptr()) };
        if mp.is_null() {
            pool.mp = unsafe {
                sys::rte_mempool_create(
                    c_name.as_ptr(),
                    n,
                    elt_size,
                    cache_size,
                    0,
                    None,
                    null_mut(),
                    None,
                    null_mut(),
                    socket_id,
                    0,
                )
            };
            if pool.mp.is_null() {
                panic!("failed to create a memory pool.");
            }
        } else {
            pool.mp = mp;
        }

        //TODO: my own cache implementation
        pool.socket_id = socket_id;
        pool.name = name.to_string();

        log::debug!("created memory pool: {}", pool.name);

        pool
    }

    #[inline(always)]
    pub fn allocate(&self) -> *mut u8 {
        let mut obj: [*mut u8; 1] = [null_mut()];

        let _r = unsafe { sys::rte_mempool_get(self.mp, obj.as_mut_ptr().cast()) };

        obj[0]
    }

    // TODO: return result?
    #[inline(always)]
    pub fn allocate_many(&self, n: u32, objs: *mut *mut u8) -> i32 {
        unsafe { sys::rte_mempool_get_bulk(self.mp, objs.cast(), n) }
    }

    #[inline(always)]
    pub fn deallocate(obj: *mut u8) {
        let header = unsafe { &*sys::rte_mempool_get_header(obj.cast()) };
        unsafe { sys::rte_mempool_put(header.mp, obj.cast()) };
    }

    #[inline(always)]
    pub fn virt2iova(obj: *mut u8) -> u64 {
        let header = unsafe { &*sys::rte_mempool_get_header(obj.cast()) };
        header.iova
    }
}

static DROP_GUARD: Mutex<()> = Mutex::new(());

impl Drop for MemoryPool {
    fn drop(&mut self) {
        let _guard = DROP_GUARD.lock().unwrap();

        if unsafe { sys::rte_mempool_lookup(self.name.as_ptr().cast()).is_null() } {
            log::debug!("memory pool {} was already dropped", self.name);
            return;
        }

        log::debug!("memory pool {} was dropped", self.name);
        unsafe { sys::rte_mempool_free(self.mp) };
    }
}
