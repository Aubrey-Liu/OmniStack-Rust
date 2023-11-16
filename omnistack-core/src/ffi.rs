use std::ffi::*;

use crate::engine::{Context, Task};
use crate::packet::{Packet, PacketPool};

#[repr(C)]
pub struct CContext {
    node: *const c_void,
    tq: *const c_void,
}

impl Into<Context> for &CContext {
    #[inline(always)]
    fn into(self) -> Context {
        Context {
            node: self.node.cast(),
            tq: self.tq.cast(),
        }
    }
}

impl Into<CContext> for &Context {
    #[inline(always)]
    fn into(self) -> CContext {
        CContext {
            node: self.node.cast(),
            tq: self.tq.cast(),
        }
    }
}

impl CContext {
    #[no_mangle]
    pub extern "C" fn push_task_downstream(&self, data: *mut Packet) {
        let ctx: Context = self.into();
        ctx.push_task_downstream(data);
    }

    #[no_mangle]
    pub extern "C" fn push_task(&self, task: Task) {
        let ctx: Context = self.into();
        ctx.push_task(task);
    }
}

impl Packet {
    #[no_mangle]
    pub extern "C" fn free_packet(&mut self) {
        PacketPool::deallocate(self)
    }
}

extern "C" {
    pub(crate) fn rte_eal_init(argc: c_int, argv: *const *const c_char) -> c_int;

    pub(crate) fn rte_malloc_socket(
        ty: *const c_char,
        size: usize,
        align: c_uint,
        socket: c_int,
    ) -> *mut c_void;

    // pub(crate) fn rte_free(ptr: *mut c_void);

    pub(crate) fn numa_node_of_cpu(cpu: c_int) -> c_int;
}
