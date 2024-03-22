use std::cell::Cell;
use std::io::{ErrorKind::*, Read, Write};
use std::net::SocketAddr;
use std::os::fd::{AsRawFd, FromRawFd};
use std::time::Duration;

use mio::net::{UnixListener, UnixStream};
use mio::{Events, Interest, Poll, Token};
use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::engine::*;
use crate::Result;

pub const DEFAULT_SOCKET_PATH: &str = "/var/run/omnistack.sock";

pub(crate) struct Server {}

#[derive(Debug, Serialize, Deserialize)]
pub enum Request {
    ThreadEnter,
    ThreadExit { thread_id: u32 },
    FdCreate { sock_addr: SocketAddr },
    FdClose { fd: OwnedFd },
}

#[repr(transparent)]
#[derive(Debug, Serialize, Deserialize)]
pub struct OwnedFd {
    fd: u32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Response {
    pub data: std::result::Result<ResponseData, ServiceError>,
}

#[derive(Debug, Serialize, Deserialize)]
pub enum ResponseData {
    None,
    ThreadEnter { thread_id: u32 },
    FdCreate { fd: OwnedFd },
}

#[derive(Debug, Serialize, Deserialize, Error)]
pub enum ServiceError {
    #[error("io error")]
    IoError,

    #[error("invalid request")]
    InvalidRequest,

    #[error("json parse error")]
    JsonParseError,

    #[error("unknown error happened")]
    Unknown,
}

pub(crate) static mut SERVER_UP_FLAG: bool = false;

impl Server {
    pub fn run() -> std::io::Result<()> {
        const LISTENER: Token = Token(0);

        let mut buf = vec![0; 512];

        let _ = std::fs::remove_file(DEFAULT_SOCKET_PATH);

        let mut poll = Poll::new()?;
        let mut events = Events::with_capacity(MAX_THREAD_NUM + 1);
        let mut listener = UnixListener::bind(DEFAULT_SOCKET_PATH)?;

        poll.registry()
            .register(&mut listener, LISTENER, Interest::READABLE)?;

        // inform the main thread that it's ready
        unsafe { SERVER_UP_FLAG = true };

        log::debug!("server up");

        while !should_stop() {
            poll.poll(&mut events, Some(Duration::new(1, 0)))?;

            for event in events.iter() {
                match event.token() {
                    LISTENER => {
                        while let Ok((mut stream, _addr)) = listener.accept() {
                            let fd = stream.as_raw_fd();
                            poll.registry().register(
                                &mut stream,
                                Token(fd as usize),
                                Interest::READABLE,
                            )?;

                            std::mem::forget(stream);
                        }
                    }
                    Token(fd) => {
                        let mut stream = unsafe { UnixStream::from_raw_fd(fd as i32) };
                        let n = match stream.read(&mut buf) {
                            Ok(n) => n,
                            Err(ref e) => {
                                if !matches!(e.kind(), WouldBlock | Interrupted) {
                                    log::error!("fd {} error {}", fd, e);
                                    poll.registry().deregister(&mut stream)?;
                                }
                                continue;
                            }
                        };

                        let res = if let Ok(req) = serde_json::from_slice::<Request>(&buf[0..n]) {
                            log::debug!("fd {} receive request: {:?}", fd, req);
                            match req {
                                Request::ThreadEnter => ThreadId::next()
                                    .ok_or(ServiceError::Unknown)
                                    .map(|id| ResponseData::ThreadEnter { thread_id: id }),
                                Request::ThreadExit { thread_id } => {
                                    ThreadId::free(thread_id);
                                    Ok(ResponseData::None)
                                }
                                _ => Err(ServiceError::InvalidRequest),
                            }
                        } else {
                            log::error!("fd {} received invalid request", fd);
                            Err(ServiceError::InvalidRequest)
                        };

                        let res = Response { data: res };
                        let buf = serde_json::to_vec(&res).expect("failed to parse json");

                        match stream.write_all(&buf) {
                            Ok(_) => continue,
                            Err(ref e) => {
                                if !matches!(e.kind(), WouldBlock | Interrupted) {
                                    log::error!("fd {} error {}", fd, e);
                                    poll.registry().deregister(&mut stream)?;
                                }
                            }
                        }
                    }
                }
            }
        }

        // remove socket forcefully
        let _ = std::fs::remove_file(DEFAULT_SOCKET_PATH);

        log::debug!("server down");

        Ok(())
    }
}

// would block
pub fn send_request(req: &Request) -> Result<Response> {
    const MAX_RETRIES: usize = 5;

    let mut buf = vec![0; 512];

    let mut stream = std::os::unix::net::UnixStream::connect(DEFAULT_SOCKET_PATH)?;
    stream.set_read_timeout(Some(Duration::new(1, 0)))?;
    stream.set_write_timeout(Some(Duration::new(1, 0)))?;

    for _i in 0..MAX_RETRIES {
        match stream.write_all(&serde_json::to_vec(req)?) {
            Ok(_) => {}
            Err(ref e) => {
                if matches!(e.kind(), WouldBlock | Interrupted) {
                    continue;
                } else {
                    break;
                }
            }
        }

        let n = match stream.read(&mut buf) {
            Ok(n) => n,
            Err(ref e) => {
                if matches!(e.kind(), WouldBlock | Interrupted) {
                    continue;
                } else {
                    break;
                }
            }
        };
        let res: Response = serde_json::from_slice(&buf[0..n])?;

        log::debug!("received response {:?}", res);

        return Ok(res);
    }

    Err(ServiceError::IoError.into())
}

pub const MAX_THREAD_NUM: usize = 2048;

static mut NEXT_THREAD_ID: usize = 0;
static mut THREAD_ID_USED: [bool; MAX_THREAD_NUM] = [false; MAX_THREAD_NUM];

pub struct ThreadId;

impl ThreadId {
    thread_local! {
        static THREAD_ID: Cell<u32> = const { Cell::new(u32::MAX) };
    }

    #[inline(always)]
    pub fn set(id: u32) {
        Self::THREAD_ID.set(id);
    }

    #[inline(always)]
    pub fn get() -> u32 {
        Self::THREAD_ID.get()
    }

    pub fn is_valid() -> bool {
        Self::THREAD_ID.get() != u32::MAX
    }

    pub(crate) fn next() -> Option<u32> {
        unsafe {
            for _ in 0..MAX_THREAD_NUM {
                if THREAD_ID_USED[NEXT_THREAD_ID] {
                    NEXT_THREAD_ID = (NEXT_THREAD_ID + 1) % MAX_THREAD_NUM;
                } else {
                    let next_id = NEXT_THREAD_ID;
                    NEXT_THREAD_ID = (NEXT_THREAD_ID + 1) % MAX_THREAD_NUM;
                    THREAD_ID_USED[next_id] = true;
                    return Some(next_id as u32);
                }
            }
        }

        None
    }

    pub(crate) fn free(id: u32) {
        unsafe { THREAD_ID_USED[id as usize] = false }
    }
}
