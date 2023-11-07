pub use crate::ffi::omnistack::memory::*;

pub fn start_control_plane() {
    #[cfg(feature = "dpdk")]
    StartControlPlane(true);

    #[cfg(not(feature = "dpdk"))]
    StartControlPlane();
}

// Supports only one test at a time.
#[cfg(test)]
mod test {
    use super::*;
    use cxx::let_cxx_string;

    #[test]
    fn test_run_control_plane() {
        start_control_plane();
        std::thread::sleep(std::time::Duration::from_millis(10));
        assert!(GetControlPlaneStatus() == ControlPlaneStatus::kRunning);
    }

    #[test]
    fn test_memory_pool() {
        use std::pin::Pin;

        start_control_plane();
        InitializeSubsystem(0.into(), false);
        InitializeSubsystemThread();
        BindedCPU(0.into());

        let_cxx_string!(pool_name = "test");
        let pool = AllocateMemoryPool(&pool_name, 32, 1024);
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
