use std::ffi::{c_char, c_int};

extern "C" {
    pub fn rte_eal_init(argc: c_int, argv: *mut *mut c_char) -> c_int;
}
