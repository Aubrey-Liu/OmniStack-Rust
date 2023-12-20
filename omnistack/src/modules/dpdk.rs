use omnistack_core::{prelude::*, protocols::MacAddr};
use omnistack_sys::dpdk as sys;

const BURST_SIZE: usize = 32;

#[repr(C)]
#[derive(Debug)]
pub struct Dpdk {
    tx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    tx_buf_items: usize,

    rx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    rx_buf_items: usize,
}

impl Dpdk {
    fn new() -> Self {
        Self {
            tx_bufs: [std::ptr::null_mut(); BURST_SIZE],
            tx_buf_items: 0,

            rx_bufs: [std::ptr::null_mut(); BURST_SIZE],
            rx_buf_items: 0,
        }
    }

    fn recv_one(&mut self, ctx: &Context) -> Option<&'static mut Packet> {
        if self.rx_buf_items > 0 {
            let pkt = ctx.meta_packet_alloc().unwrap();
            pkt.init_from_mbuf(self.rx_bufs[self.rx_buf_items - 1]);
            pkt.refcnt = 1;
            pkt.offset = 0;

            self.rx_buf_items -= 1;

            Some(pkt)
        } else {
            None
        }
    }
}

impl IoAdapter for Dpdk {
    // might be called multiple times!
    fn init(&mut self, ctx: &Context, port: u16, num_queues: u16) -> Result<MacAddr> {
        let ret = unsafe { sys::dev_port_init(port, num_queues, ctx.pktpool.pktmbuf) };

        if ret != 0 {
            return Err(Error::Unknown);
        }

        let mut mac_addr = sys::rte_ether_addr { addr_bytes: [0; 6] };
        let ret = unsafe { sys::dev_macaddr_get(port, &mut mac_addr) };

        log::info!(
            "Port {port} MAC addr: {:?}",
            MacAddr::from_bytes(mac_addr.addr_bytes)
        );

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

        self.tx_bufs[self.tx_buf_items] = packet.mbuf.inner();
        self.tx_buf_items += 1;

        // packet's meta info is not needed anymore
        ctx.meta_packet_dealloc(packet);

        // flush when buffer fills up
        if self.tx_buf_items == BURST_SIZE {
            while self.tx_buf_items > 0 {
                let nb_tx = unsafe {
                    sys::dev_send_packet(0, 0, self.tx_bufs.as_mut_ptr(), BURST_SIZE as _)
                };
                self.tx_buf_items -= nb_tx as usize;

                log::debug!("Sent {nb_tx} packets to the NIC");
            }
        }

        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&mut Packet> {
        self.recv_one(ctx)
            .or_else(|| {
                let rx = unsafe {
                    sys::dev_recv_packet(0, 0, self.rx_bufs.as_mut_ptr(), BURST_SIZE as _)
                };

                if rx > 0 {
                    log::debug!("Received {rx} packets from the NIC");

                    self.rx_buf_items = rx as _;
                }

                self.recv_one(ctx)
            })
            .ok_or(Error::NoData)
    }
}

register_adapter!(Dpdk);
