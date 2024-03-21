#![allow(unused)]

use std::io::{self, Read, Write};
use std::net::{IpAddr, SocketAddr};
use std::os::unix::net::{UnixListener, UnixStream};
use std::ptr::null_mut;

use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::engine::*;
use crate::module::*;
use crate::packet::*;
use crate::protocol::*;

pub const BURST_SIZE: usize = 32;
pub const DEFAULT_SOCKET_PATH: &str = "/var/run/omnistack.sock";

#[derive(omnistack_proc::Module)]
#[repr(align(64))]
struct UserNode {
    rx_queue: [*mut Packet; BURST_SIZE],
}

impl UserNode {
    fn new() -> Self {
        UserNode {
            rx_queue: [null_mut(); BURST_SIZE],
        }
    }
}

impl Module for UserNode {
    fn process(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        // TODO: deliver the packet to user
        PacketPool::deallocate(packet);

        // NOTE: send packet to user, but re-collect mbufs on the server side
        // to ensure efficient cache utilization

        Err(ModuleError::Dropped)
    }

    #[cfg(not(feature = "rxonly"))]
    fn poll(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        // TODO: receive from users

        ctx.pktpool
            .allocate_many(BURST_SIZE as _, &mut self.rx_queue)?;

        let mut ret = null_mut();
        for &pkt in self.rx_queue.iter().rev() {
            let pkt = unsafe { &mut *pkt };
            // let size = 1500 - 28;
            let size = 64 - 28;

            pkt.data = pkt.buf.0.as_mut_ptr();
            pkt.buf_ty = PktBufType::Local;
            pkt.offset = DEFAULT_OFFSET as _;
            pkt.set_len(size as _);
            pkt.nic = 0; // TODO: read from user config
            pkt.refcnt = 1;
            pkt.next = ret;
            ret = pkt;
        }

        Ok(unsafe { &mut *ret })
    }

    fn capability(&self) -> ModuleCapa {
        ModuleCapa::PROCESS | ModuleCapa::POLL
    }
}

pub(crate) struct Server {}

#[derive(Debug, Serialize, Deserialize)]
pub enum Request {
    UserNew(SocketAddr), // TODO: info
    UserQuit,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Response(std::result::Result<ResponseData, ServerError>);

#[derive(Debug, Serialize, Deserialize)]
pub enum ResponseData {
    UserNew {
        thread_id: u32,
        pktpool_raw_addr: usize,
        channel_raw_addr: usize,
    },
    UserQuit,
}

#[derive(Serialize, Deserialize, Debug, Error)]
pub enum ServerError {
    #[error("invalid request format")]
    InvalidReq,

    #[error("memory exhausted")]
    OutofMemory,

    #[error("unknown error happened")]
    Unknown,
}

impl Server {
    pub fn run() {
        let _ = std::fs::remove_file(DEFAULT_SOCKET_PATH);

        let listener = UnixListener::bind(DEFAULT_SOCKET_PATH).unwrap();
        listener.set_nonblocking(true).unwrap();

        while unsafe { !STOP_FLAG } {
            let (stream, addr) = match listener.accept() {
                Ok(x) => x,
                Err(e) => {
                    if e.kind() == io::ErrorKind::WouldBlock {
                        // std::thread::yield_now();

                        continue;
                    } else {
                        log::error!("unexpected error: {:?}", e);

                        break;
                    }
                }
            };

            log::info!("accept connection from: {:?}", addr);

            let _ = std::thread::spawn(|| Self::client_handler(stream));
        }

        // remove socket forcefully
        let _ = std::fs::remove_file(DEFAULT_SOCKET_PATH);

        log::debug!("server exiting...");
    }

    fn client_handler(mut stream: UnixStream) -> std::result::Result<(), io::Error> {
        stream.set_read_timeout(Some(std::time::Duration::new(3, 0)))?;
        // stream.set_write_timeout(Some(std::time::Duration::from_millis(1)))?;

        while unsafe { !STOP_FLAG } {
            let mut buf = vec![0; 1024];
            let n = stream.read(&mut buf)?;

            let req: Request = serde_json::from_slice(&buf[0..n])?;
            dbg!(req);

            let res = Response(Err(ServerError::Unknown));
            let buf = serde_json::to_vec(&res)?;

            let _ = stream.write(&buf)?;
        }

        Ok(())
    }
}

// #[derive(Debug, Serialize, Deserialize)]
// pub enum TransportLayerKind {
//     UDP,
//     TCP,
// }
//
// #[derive(Debug, Serialize, Deserialize)]
// pub enum NetworkLayerKind {
//     V4,
//     V6,
// }

// #[derive(Debug, Serialize, Deserialize)]
// pub struct UserInfo {
//     // pktpool: PacketPool,
//     pub l3_kind: NetworkLayerKind,
//     pub l4_kind: TransportLayerKind,
//
//     // TODO: include Ipv6 maybe
//
//     pub src_ip: IpAddr,
//     pub dst_ip: IpAddr,
// }
