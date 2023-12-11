use omnistack_core::{prelude::*, protocols::MacAddr};
use omnistack_sys::dpdk as sys;

// todo: default value is 32 (3 is for debugging)
const BURST_SIZE: usize = 3;

#[repr(C)]
#[derive(Debug)]
pub struct Dpdk {
    buffers: [*mut sys::rte_mbuf; BURST_SIZE],
    buf_idx: usize,
}

impl Dpdk {
    fn new() -> Self {
        Self {
            buffers: [std::ptr::null_mut(); BURST_SIZE],
            buf_idx: 0,
        }
    }
}

impl IoAdapter for Dpdk {
    // might be called multiple times!
    fn init(&mut self, ctx: &Context, port: u16, num_queues: u16) -> Result<MacAddr> {
        let ret = unsafe { sys::dev_port_init(port, num_queues, ctx.pktpool.pktpool) };

        if ret != 0 {
            return Err(Error::Unknown);
        }

        let mut mac_addr = sys::rte_ether_addr { addr_bytes: [0; 6] };
        let ret = unsafe { sys::dev_macaddr_get(port, &mut mac_addr) };

        if ret != 0 {
            Err(Error::Unknown)
        } else {
            Ok(MacAddr::from_bytes(mac_addr.addr_bytes))
        }
    }

    // todo: buffer and flush
    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        // set everything for mbuf
        packet.mbuf.inc_data_off(packet.offset);
        packet.mbuf.set_data_len(packet.len());
        packet.mbuf.set_pkt_len(packet.len() as _);

        packet.mbuf.set_l2_len(packet.l2_header.length);
        packet.mbuf.set_l3_len(packet.l3_header.length);
        packet.mbuf.set_l4_len(packet.l4_header.length);

        self.buffers[self.buf_idx] = packet.mbuf.inner();
        self.buf_idx += 1;

        // packet's meta info is not needed anymore
        ctx.meta_packet_dealloc(packet);

        // flush when buffers are full
        if self.buf_idx == BURST_SIZE {
            while self.buf_idx > 0 {
                let nb_tx = unsafe {
                    sys::dev_send_packet(0, 0, self.buffers.as_mut_ptr(), BURST_SIZE as _)
                };
                self.buf_idx -= nb_tx as usize;
            }
        }

        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&mut Packet> {
        let mut bufs = [std::ptr::null_mut(); 1];

        let rx = unsafe { sys::dev_recv_packet(0, 0, bufs.as_mut_ptr(), 1) };

        if rx > 0 {
            let pkt = ctx.meta_packet_alloc().unwrap();
            pkt.init_from_mbuf(bufs[0]);
            pkt.refcnt = 1;
            pkt.offset = 0;

            Ok(pkt)
        } else if rx == 0 {
            Err(Error::NoData)
        } else {
            Err(Error::Unknown)
        }
    }
}

register_adapter!(Dpdk);
