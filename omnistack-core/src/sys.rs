use std::ffi::CString;

use dpdk_sys as sys;

pub fn process_init() {
    let args = ["", "--log-level", "warning", "--proc-type", "auto"];

    let args_c: Vec<_> = args.iter().map(|&s| CString::new(s).unwrap()).collect();
    let mut args_raw: Vec<_> = args_c.iter().map(|s| s.as_ptr()).collect();

    let ret = unsafe { sys::rte_eal_init(args_raw.len() as _, args_raw.as_mut_ptr().cast()) };
    if ret < 0 {
        panic!("rte_eal_init failed");
    }
}
