#![allow(dead_code)]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(improper_ctypes)]

pub mod sys {
    pub use dpdk_sys::*;

    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub use sys::*;
