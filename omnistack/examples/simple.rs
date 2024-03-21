use std::{
    io::Read,
    io::Write,
    net::{IpAddr, Ipv4Addr},
    os::unix::net::UnixStream,
};

use omnistack_core::user::*;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // let req = Request::UserNew(UserInfo {
    //     l3_kind: NetworkLayerKind::V4,
    //     l4_kind: TransportLayerKind::UDP,
    //     src_ip: IpAddr::V4(Ipv4Addr::new(192, 168, 0, 1)),
    //     dst_ip: IpAddr::V4(Ipv4Addr::new(192, 168, 0, 2)),
    // });
    //
    // let res = omnistack::send_request(&req)?;

    Ok(())
}
