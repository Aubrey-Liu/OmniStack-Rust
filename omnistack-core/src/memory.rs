use std::cell::Cell;
use std::cmp::max;
use std::ffi::CString;
use std::mem::size_of;
use std::ptr::null_mut;
use std::sync::atomic::AtomicU32;
use std::sync::Mutex;

use dpdk_sys as sys;
use sys::constants::*;
use thiserror::Error;

#[inline(always)]
pub fn next_thread_id() -> u32 {
    static NEXT_THREAD_ID: AtomicU32 = AtomicU32::new(0);

    NEXT_THREAD_ID.fetch_add(1, std::sync::atomic::Ordering::SeqCst)
}

pub struct ThreadId;

impl ThreadId {
    thread_local! {
        static THREAD_ID: Cell<u32> = Cell::new(u32::MAX);
    }

    #[inline(always)]
    pub fn set(id: u32) {
        Self::THREAD_ID.set(id);
    }

    #[inline(always)]
    pub fn get() -> u32 {
        Self::THREAD_ID.get()
    }

    pub fn is_valid() -> bool {
        Self::THREAD_ID.get() != u32::MAX
    }
}

pub const CACHE_LINE_SIZE: usize = 64;

const MAX_THREAD_NUM: usize = 256;
const MEMPOOL_HDR_OFFSET: usize = size_of::<sys::rte_mempool_objhdr>();

#[repr(C)]
struct MemoryPoolPrivData {
    cache_per_thread: *const *mut sys::rte_mempool_cache,
}

#[repr(C)]
pub struct MemoryPool {
    pub mp: *mut sys::rte_mempool,
    pub socket_id: u32,
    // pub name: [char; 32],
    pub cache_per_thread: *mut *mut sys::rte_mempool_cache,
}

#[derive(Debug, Error)]
pub enum MemoryError {
    #[error("memory is exhausted")]
    Exhausted,
}

static MEMPOOL_INIT_GUARD: Mutex<()> = Mutex::new(());

impl MemoryPool {
    pub unsafe fn get_or_create(
        name: &str,
        n: u32,
        elt_size: u32,
        socket_id: u32,
    ) -> Result<&'static mut Self, MemoryError> {
        assert!(name.len() <= 24, "name too long");

        let _guard = MEMPOOL_INIT_GUARD.lock().unwrap();

        let zone_name = CString::new(format!("zone_{}", name)).unwrap();

        let zone = sys::rte_memzone_lookup(zone_name.as_ptr());
        if !zone.is_null() {
            let mempool = (*zone)
                .__bindgen_anon_1
                .addr
                .cast::<MemoryPool>()
                .as_mut()
                .unwrap();

            return Ok(mempool);
        }

        let memzone_len =
            max(size_of::<MemoryPool>(), CACHE_LINE_SIZE) + size_of::<usize>() * MAX_THREAD_NUM;

        let zone = sys::rte_memzone_reserve_aligned(
            zone_name.as_ptr(),
            memzone_len as _,
            socket_id as _,
            RTE_MEMZONE_1GB | RTE_MEMZONE_SIZE_HINT_ONLY,
            CACHE_LINE_SIZE as _,
        );
        if zone.is_null() {
            return Err(MemoryError::Exhausted);
        }

        let pool_name = CString::new(format!("pool_{}", name)).unwrap();
        let mp = sys::rte_mempool_create(
            pool_name.as_ptr(),
            n,
            elt_size,
            0, // use custom cache
            max(size_of::<MemoryPoolPrivData>(), CACHE_LINE_SIZE) as _,
            None,
            null_mut(),
            None,
            null_mut(),
            socket_id as _,
            0,
        );
        if mp.is_null() {
            return Err(MemoryError::Exhausted);
        }

        let memory_pool = &mut *(*zone).__bindgen_anon_1.addr.cast::<MemoryPool>();
        memory_pool.mp = mp;
        memory_pool.socket_id = socket_id;

        let trailer = (memory_pool as *mut MemoryPool).add(1).cast::<u8>();
        memory_pool.cache_per_thread = trailer.add(trailer.align_offset(CACHE_LINE_SIZE)).cast();

        for i in 0..MAX_THREAD_NUM {
            memory_pool
                .cache_per_thread
                .add(i)
                .write(usize::MAX as *mut _);
        }

        let priv_data = sys::rte_mempool_get_priv(mp)
            .cast::<MemoryPoolPrivData>()
            .as_mut()
            .unwrap();
        priv_data.cache_per_thread = memory_pool.cache_per_thread.cast_const();

        Ok(memory_pool)
    }

    pub unsafe fn create_cache(&mut self, cache_size: u32, thread_id: u32) {
        self.cache_per_thread
            .add(thread_id as usize)
            .write(sys::rte_mempool_cache_create(
                cache_size,
                self.socket_id as _,
            ));
    }

    #[inline(always)]
    pub unsafe fn allocate(
        &self,
        objs: &mut [*mut u8],
        n: u32,
        thread_id: u32,
    ) -> Result<(), MemoryError> {
        let cache = self.get_cache(thread_id);

        let r = sys::rte_mempool_generic_get(self.mp, objs.as_mut_ptr().cast(), n, cache);
        if r == 0 {
            Ok(())
        } else {
            Err(MemoryError::Exhausted)
        }
    }

    #[inline(always)]
    pub(crate) unsafe fn deallocate(obj: *mut u8, thread_id: u32) {
        let objs = [obj];
        let sys_mp = (*obj
            .sub(MEMPOOL_HDR_OFFSET)
            .cast::<sys::rte_mempool_objhdr>())
        .mp;

        // Based on the fact that cache size was set to 0
        let priv_data = &*sys_mp
            .cast::<u8>()
            .add(size_of::<sys::rte_mempool>())
            .cast::<MemoryPoolPrivData>();
        let cache = priv_data.cache_per_thread.add(thread_id as usize).read();

        unsafe { sys::rte_mempool_generic_put(sys_mp, objs.as_ptr().cast(), 1, cache) };
    }

    #[inline(always)]
    pub fn virt2iova(obj: *mut u8) -> u64 {
        let header = unsafe {
            &*obj
                .sub(MEMPOOL_HDR_OFFSET)
                .cast::<sys::rte_mempool_objhdr>()
        };
        header.iova
    }

    #[inline(always)]
    unsafe fn get_cache(&self, thread_id: u32) -> *mut sys::rte_mempool_cache {
        self.cache_per_thread.add(thread_id as usize).read()
    }
}

/// An useful generic type for having cache aligned fields in structs.
#[repr(align(64))]
pub struct Aligned<T>(pub T);

impl<T> From<T> for Aligned<T> {
    fn from(value: T) -> Self {
        Aligned(value)
    }
}

impl<T: Clone> Clone for Aligned<T> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

impl<T: Copy> Copy for Aligned<T> {}
