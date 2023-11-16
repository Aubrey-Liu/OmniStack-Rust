use std::ffi::*;
use std::ptr::NonNull;

pub struct MemoryPool<T> {
    data: *mut Block<T>,
    len: usize,
    free_list: Option<NonNull<Block<T>>>,
}

#[repr(C)]
struct Block<T> {
    data: T,
    next_free: Option<NonNull<Block<T>>>,
}

impl<T> MemoryPool<T> {
    pub const fn empty() -> Self {
        Self {
            data: std::ptr::null_mut(),
            len: 0,
            free_list: None,
        }
    }

    pub fn init(&mut self, capacity: usize, core_id: i32) {
        let arg0 = CString::new("").unwrap();
        let argv = [arg0.as_ptr()];
        let ret = unsafe { crate::ffi::rte_eal_init(1, argv.as_ptr().cast()) };
        if ret != 0 {
            panic!("failed to init dpdk");
        }

        self.data = unsafe {
            crate::ffi::rte_malloc_socket(
                std::ptr::null(),
                std::mem::size_of::<Block<T>>() * capacity,
                std::mem::align_of::<Block<T>>() as c_uint,
                crate::ffi::numa_node_of_cpu(core_id),
            ).cast()
        };

        self.len = capacity;
        self.free_list = self.build_free_list();
    }

    fn build_free_list(&mut self) -> Option<NonNull<Block<T>>> {
        let mut current = self.data;
        let mut free_list = None;

        for _ in 0..self.len {
            let block = unsafe { current.as_mut().unwrap() };
            block.next_free = free_list;
            free_list = NonNull::new(current);
            current = unsafe { current.add(1) };
        }

        free_list
    }

    pub fn allocate(&mut self) -> Option<*mut T> {
        self.free_list.map(|mut node| {
            self.free_list = unsafe { node.as_ref().next_free };
            let data = unsafe { &mut node.as_mut().data };
            data as *mut _
        })
    }

    pub fn deallocate(&mut self, item: *mut T) {
        let block = item as *mut Block<T>;
        let node = NonNull::new(block);

        if let Some(mut node) = node {
            unsafe { node.as_mut().next_free = self.free_list };
            self.free_list = Some(node);
        }
    }
}

unsafe impl<T> Send for MemoryPool<T> {}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_memory_pool_exhaustive() {
        let size = 2048;
        let mut pool: MemoryPool<usize> = MemoryPool::empty();
        pool.init(size, 0);

        let mut ptrs = Vec::new();
        for _ in 0..size {
            ptrs.push(pool.allocate().unwrap())
        }
        assert_eq!(pool.allocate(), None, "pool is exhausted");

        let ptr = ptrs.pop().unwrap();
        pool.deallocate(ptr);
        assert_eq!(pool.allocate(), Some(ptr), "pool has only one block");
        pool.deallocate(ptr);

        while let Some(ptr) = ptrs.pop() {
            pool.deallocate(ptr)
        }
    }
}
