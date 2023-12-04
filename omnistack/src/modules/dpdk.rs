use omnistack_core::{prelude::*, register_adapter};
use omnistack_sys::dpdk as sys;

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
    fn init(&mut self, ctx: &Context, port: u16, num_queues: u16) -> Result<()> {
        let ret = unsafe { sys::dev_port_init(port, num_queues, ctx.pktpool.mempool) };

        if ret == 0 {
            Ok(())
        } else {
            Err(Error::Unknown)
        }
    }

    // todo: buffer and flush
    fn send(&mut self, _ctx: &Context, packet: &mut Packet) -> Result<()> {
        // set everything for mbuf
        packet.mbuf.inc_data_off(packet.data_offset());
        packet.mbuf.set_data_len(packet.len());
        packet.mbuf.set_pkt_len(packet.len() as _);

        // todo: set the length of l2, l3, l4 headers
        packet.mbuf.set_l2_len(packet.l2_header.length);
        packet.mbuf.set_l3_len(packet.l3_header.length);
        packet.mbuf.set_l4_len(packet.l4_header.length);

        self.buffers[self.buf_idx] = packet.mbuf.inner();
        self.buf_idx += 1;

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

    fn recv(&mut self, _ctx: &Context) -> Result<&mut Packet> {
        let mut bufs = [std::ptr::null_mut(); 1];

        let rx = unsafe { sys::dev_recv_packet(0, 0, bufs.as_mut_ptr(), 1) };

        if rx > 0 {
            Ok(Packet::from_mbuf(bufs[0]))
        } else if rx == 0 {
            Err(Error::NoData)
        } else {
            Err(Error::Unknown)
        }
    }
}

register_adapter!(Dpdk);
