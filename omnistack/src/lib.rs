use std::{
    io::{self, Read, Write},
    os::unix::net::UnixStream,
    sync::Once,
};

use omnistack_core::{
    sys::process_init,
    user::{Request, Response, DEFAULT_SOCKET_PATH},
};

pub mod block;
pub mod udp;

pub(crate) fn init() {
    // init process for once
    static INIT_ONCE: Once = Once::new();
    INIT_ONCE.call_once(process_init);
}

// would block
pub fn send_request(req: &Request) -> Result<Response, io::Error> {
    let mut stream = UnixStream::connect(DEFAULT_SOCKET_PATH)?;

    let buf = serde_json::to_vec(req)?;
    stream.write_all(&buf)?;

    let mut buf = vec![0; 1024];
    let n = stream.read(&mut buf)?;

    let res: Response = serde_json::from_slice(&buf[0..n])?;

    Ok(res)
}
