use std::ffi::*;

extern "C" {
    pub fn rte_eal_init(argc: c_int, argv: *const *const c_char) -> c_int;

    pub fn rte_malloc_socket(
        ty: *const c_char,
        size: usize,
        align: c_uint,
        socket: c_int,
    ) -> *mut c_void;

    pub fn rte_free(ptr: *mut c_void);
}

extern "C" {
    pub fn numa_node_of_cpu(cpu: c_int) -> c_int;
}
