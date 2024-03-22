use std::sync::Once;

use omnistack_core::sys::process_init;

pub mod block;
pub mod udp;

pub(crate) fn init() {
    // init process for once
    static INIT_ONCE: Once = Once::new();
    INIT_ONCE.call_once(process_init);
}
