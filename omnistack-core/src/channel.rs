// #![allow(unused)]

use std::{ffi::CString, ptr::null_mut};

use dpdk_sys as sys;
use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ChannelError {
    #[error("buffer is full")]
    WriteFull,

    #[error("buffer is empty")]
    ReadEmpty,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct RawIpcChannel {
    ring: usize, // type: *mut rte_ring
}

pub struct IpcChannel {
    ring: *mut sys::rte_ring,
}

impl IpcChannel {
    pub fn new(name: &str, size: u32, socket_id: u32) -> Self {
        let name = CString::new(name).unwrap();

        Self {
            ring: unsafe { sys::rte_ring_create(name.as_ptr(), size, socket_id as _, 0) },
        }
    }

    pub fn from_raw(raw_channel: RawIpcChannel) -> Self {
        Self {
            ring: raw_channel.ring as *mut _,
        }
    }

    pub fn into_raw(self) -> RawIpcChannel {
        RawIpcChannel {
            ring: self.ring as usize,
        }
    }

    pub fn send(&self, obj: *mut u8) -> Result<(), ChannelError> {
        let r = unsafe { sys::rte_ring_mp_enqueue(self.ring, obj.cast()) };

        if r == 0 {
            Ok(())
        } else {
            Err(ChannelError::WriteFull)
        }
    }

    pub fn recv(&self) -> Result<*mut u8, ChannelError> {
        let mut objs = [null_mut(); 1];

        let r = unsafe { sys::rte_ring_mc_dequeue(self.ring, objs.as_mut_ptr()) };

        if r == 0 {
            let obj = unsafe { *objs.get_unchecked(0).cast() };

            Ok(obj)
        } else {
            Err(ChannelError::ReadEmpty)
        }
    }
}
