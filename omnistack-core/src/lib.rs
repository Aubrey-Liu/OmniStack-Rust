pub mod modules;

use autocxx::prelude::*;

include_cpp! {
    #include "omnistack/memory/memory.h"
    safety!(unsafe)
    generate_ns!("omnistack::memory")
}

#[cfg(test)]
mod test {
    use super::ffi::omnistack::memory;
    use cxx::let_cxx_string;

    #[test]
    fn test_run_control_plane() {
        #[cfg(feature = "dpdk")]
        memory::StartControlPlane(true);

        #[cfg(not(feature = "dpdk"))]
        memory::StartControlPlane();

        std::thread::sleep(std::time::Duration::from_millis(10));

        assert!(memory::GetControlPlaneStatus() == memory::ControlPlaneStatus::kRunning);
    }

    #[test]
    fn test_memory_pool() {
        use std::pin::Pin;

        memory::StartControlPlane(true);
        memory::InitializeSubsystem(0.into(), false);
        memory::InitializeSubsystemThread();
        memory::BindedCPU(0.into());

        let_cxx_string!(pool_name = "test");
        let pool = memory::AllocateMemoryPool(&pool_name, 32, 1024);
        let mut pool = unsafe { Pin::new_unchecked(&mut *pool) };

        let blk1 = pool.as_mut().Get();
        let blk2 = pool.as_mut().Get();
        let blk3 = pool.as_mut().Get();

        unsafe {
            pool.as_mut().Put(blk1);
            pool.as_mut().Put(blk3);
            pool.as_mut().Put(blk2);
        }
    }
}
