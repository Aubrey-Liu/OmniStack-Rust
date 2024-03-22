use dpdk_sys as sys;

pub fn process_init() {
    let args = [c"", c"--log-level", c"warning", c"--proc-type", c"auto"];

    let mut args_ptr: Vec<_> = args.iter().map(|&s| s.as_ptr()).collect();

    let ret = unsafe { sys::rte_eal_init(args.len() as _, args_ptr.as_mut_ptr().cast()) };
    if ret < 0 {
        panic!("rte_eal_init failed");
    }
}
