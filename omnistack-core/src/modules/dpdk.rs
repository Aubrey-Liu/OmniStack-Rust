use std::ffi::{c_void, CString};
use std::mem::size_of;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Once;
use std::time::Instant;

use crate::memory::MemoryError;
use crate::nio::Adapter;
use crate::prelude::*;

use dpdk_sys as sys;
use dpdk_sys::constants::*;

const RX_RING_SIZE: u16 = 2048;
const TX_RING_SIZE: u16 = 2048;

const BURST_SIZE: usize = 64;
const PKTMBUF_CACHE_SIZE: u32 = 256;
const PKTMBUF_SIZE: u32 = 8192 - 1;
const MBUF_SIZE: usize =
    (MTU as usize + size_of::<EthHeader>() + sys::RTE_PKTMBUF_HEADROOM as usize + 64 - 1) / 64 * 64;
const MAX_FRAME_SIZE: usize = MTU as usize + size_of::<EthHeader>();

// NOTE: mut is only for signature, it's fine
static mut DEFAULT_RSS_KEY: [u8; 40] = [
    0xd1, 0x81, 0xc6, 0x2c, 0xf7, 0xf4, 0xdb, 0x5b, 0x19, 0x83, 0xa2, 0xfc, 0x94, 0x3e, 0x1a, 0xdb,
    0xd9, 0x38, 0x9e, 0x6b, 0xd1, 0x03, 0x9c, 0x2c, 0xa7, 0x44, 0x99, 0xad, 0x59, 0x3d, 0x56, 0xd9,
    0xf3, 0x25, 0x3c, 0x06, 0x2a, 0xdc, 0x1f, 0xfc,
];

#[repr(C, align(64))]
#[derive(omnistack_proc::Adapter)]
pub struct Dpdk {
    tx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    rx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    tx_idx: usize,

    nic: u16,
    port: u16,
    queue: u16,

    pktmbuf_pool: *mut sys::rte_mempool,

    #[cfg(feature = "perf")]
    stats: Stats,
}

#[cfg(feature = "perf")]
struct Stats {
    sent_pkts: u64,
    sent_bytes: u64,
    recv_pkts: u64,
    recv_bytes: u64,
    begin: Instant,
}

impl Dpdk {
    fn new() -> Self {
        unsafe { std::mem::zeroed() }
    }

    fn default_port_conf() -> sys::rte_eth_conf {
        let mut conf: sys::rte_eth_conf = unsafe { std::mem::zeroed() };
        let rxmode = &mut conf.rxmode;
        rxmode.mq_mode = sys::rte_eth_rx_mq_mode_RTE_ETH_MQ_RX_RSS;
        rxmode.mtu = MTU as _;
        rxmode.max_lro_pkt_size = MAX_FRAME_SIZE as _;
        rxmode.offloads = RTE_ETH_RX_OFFLOAD_RSS_HASH | RTE_ETH_RX_OFFLOAD_CHECKSUM;

        let txmode = &mut conf.txmode;
        txmode.mq_mode = sys::rte_eth_tx_mq_mode_RTE_ETH_MQ_TX_NONE;
        txmode.offloads = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM
            | RTE_ETH_TX_OFFLOAD_UDP_CKSUM
            | RTE_ETH_TX_OFFLOAD_TCP_CKSUM
            | RTE_ETH_TX_OFFLOAD_SCTP_CKSUM
            | RTE_ETH_TX_OFFLOAD_TCP_TSO
            | RTE_ETH_TX_OFFLOAD_UDP_TSO
            | RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

        let rx_adv_conf = &mut conf.rx_adv_conf;
        rx_adv_conf.rss_conf = sys::rte_eth_rss_conf {
            rss_key: unsafe { DEFAULT_RSS_KEY.as_mut_ptr() },
            rss_key_len: unsafe { DEFAULT_RSS_KEY.len() as _ },
            // TODO:
            rss_hf: RTE_ETH_RSS_FRAG_IPV4
                | RTE_ETH_RSS_NONFRAG_IPV4_TCP
                | RTE_ETH_RSS_NONFRAG_IPV4_UDP, // | RTE_ETH_RSS_IPV4
                                                // | RTE_ETH_RSS_TCP
                                                // | RTE_ETH_RSS_UDP,
        };

        conf
    }

    fn default_rx_conf(port: u16) -> sys::rte_eth_rxconf {
        let mut dev_info: sys::rte_eth_dev_info = unsafe { std::mem::zeroed() };
        let ret = unsafe { sys::rte_eth_dev_info_get(port, &mut dev_info) };
        if ret != 0 {
            panic!("failed to get port info");
        }

        let conf = Self::default_port_conf();
        let mut rx_conf = dev_info.default_rxconf;
        rx_conf.offloads = conf.rxmode.offloads & dev_info.rx_offload_capa;

        rx_conf
    }

    fn default_tx_conf(port: u16) -> sys::rte_eth_txconf {
        let mut dev_info: sys::rte_eth_dev_info = unsafe { std::mem::zeroed() };
        let ret = unsafe { sys::rte_eth_dev_info_get(port, &mut dev_info) };
        if ret != 0 {
            panic!("failed to get port info");
        }

        let conf = Self::default_port_conf();
        let mut tx_conf = dev_info.default_txconf;
        tx_conf.offloads = conf.txmode.offloads & dev_info.tx_offload_capa;

        tx_conf
    }

    /// This function should only be called ONCE.
    unsafe fn port_init(port: u16, num_queues: u16) {
        if sys::rte_eth_dev_is_valid_port(port) == 0 {
            panic!("invalid port number {port}");
        }

        let mut dev_info: sys::rte_eth_dev_info = std::mem::zeroed();
        let ret = sys::rte_eth_dev_info_get(port, &mut dev_info);
        if ret != 0 {
            panic!("failed to get port info");
        }

        let mut conf = Self::default_port_conf();
        conf.txmode.offloads &= dev_info.tx_offload_capa;
        conf.rxmode.offloads &= dev_info.rx_offload_capa;

        let mut rx_desc = RX_RING_SIZE;
        let mut tx_desc = TX_RING_SIZE;
        let ret = sys::rte_eth_dev_adjust_nb_rx_tx_desc(port, &mut rx_desc, &mut tx_desc);
        if ret != 0 {
            panic!("failed to set descriptor numbers");
        }
        log::debug!(
            "[Port {}] rx queue: {} desc, tx queue: {} desc",
            port,
            rx_desc,
            tx_desc
        );

        let ret = sys::rte_eth_dev_configure(port, num_queues, num_queues, &conf);
        if ret != 0 {
            panic!("failed to configurate device");
        }

        let ret = sys::rte_flow_flush(port, null_mut());
        if ret != 0 {
            panic!("failed to flush flow");
        }

        let socket_id = sys::rte_socket_id();
        let dev_socket_id = sys::rte_eth_dev_socket_id(port);
        assert_eq!(socket_id, dev_socket_id as u32);

        for queue in 0..num_queues {
            let name = CString::new(format!("pktmbuf_p{}q{}", port, queue)).unwrap();

            let pktmbuf_pool = sys::rte_pktmbuf_pool_create(
                name.as_ptr(),
                PKTMBUF_SIZE,
                PKTMBUF_CACHE_SIZE,
                0,
                MBUF_SIZE as _,
                socket_id as _,
            );

            let rx_conf = Self::default_rx_conf(port);
            let ret = sys::rte_eth_rx_queue_setup(
                port,
                queue,
                rx_desc,
                socket_id,
                &rx_conf,
                pktmbuf_pool,
            );
            if ret != 0 {
                panic!("failed to setup rx queue {}", queue);
            }
            log::debug!("rx queue {} of port {} is ready", queue, port);

            let tx_conf = Self::default_tx_conf(port);
            let ret = sys::rte_eth_tx_queue_setup(port, queue, tx_desc, socket_id, &tx_conf);
            if ret != 0 {
                panic!("failed to setup tx queue {}", queue);
            }
            log::debug!("tx queue {} of port {} is ready", queue, port);
        }

        let ret = unsafe { sys::rte_eth_dev_start(port) };
        if ret != 0 {
            panic!("failed to start port {}", port);
        }

        let ret = unsafe { sys::rte_eth_promiscuous_enable(port) };
        if ret != 0 {
            panic!("failed to enable promiscuous mode");
        }

        log::debug!("port {} started", port);

        INIT_DONE_FLAG.store(true, Ordering::Relaxed);
    }
}

unsafe extern "C" fn packet_free_callback(_addr: *mut c_void, pkt: *mut c_void) {
    PacketPool::deallocate(pkt.cast());
}

static INIT_DONE_FLAG: AtomicBool = AtomicBool::new(false);

impl Adapter for Dpdk {
    fn init(
        &mut self,
        _ctx: &Context,
        nic: u16,
        port: u16,
        num_queues: u16,
        queue: u16,
    ) -> Result<MacAddr> {
        static INIT_ONCE: Once = Once::new();
        INIT_ONCE.call_once(|| {
            unsafe { Self::port_init(port, num_queues) };
        });

        // wait until port is initialized
        while !INIT_DONE_FLAG.load(Ordering::Relaxed) {
            log::debug!("busy waiting");
            std::hint::spin_loop();
        }

        self.nic = nic;
        self.port = port;
        self.queue = queue;

        let name = CString::new(format!("pktmbuf_p{}q{}", port, queue)).unwrap();
        self.pktmbuf_pool = unsafe { sys::rte_mempool_lookup(name.as_ptr()) };
        assert!(!self.pktmbuf_pool.is_null());

        let ret = unsafe {
            sys::rte_pktmbuf_alloc_bulk(
                self.pktmbuf_pool,
                self.tx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
            )
        };
        if ret != 0 {
            return Err(Error::Unknown);
        }

        let mut mac_addr = sys::rte_ether_addr { addr_bytes: [0; 6] };
        let ret = unsafe { sys::rte_eth_macaddr_get(port, &mut mac_addr) };
        if ret != 0 {
            return Err(Error::Unknown);
        }

        let mac_addr = MacAddr::from_bytes(mac_addr.addr_bytes);

        #[cfg(feature = "perf")]
        {
            self.stats.begin = Instant::now();
        }

        Ok(mac_addr)
    }

    fn start(&self) -> Result<()> {
        let ret = unsafe { sys::rte_eth_dev_start(self.port) };
        if ret != 0 {
            panic!("failed to start port {}", self.port);
        }

        let ret = unsafe { sys::rte_eth_promiscuous_enable(self.port) };
        if ret != 0 {
            panic!("failed to enable promiscuous mode");
        }

        log::debug!("port {} started", self.port);

        Ok(())
    }

    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        let mbuf = unsafe { &mut **self.tx_bufs.get_unchecked(self.tx_idx) };
        self.tx_idx += 1;

        let shared_info: &mut sys::rte_mbuf_ext_shared_info =
            unsafe { &mut *mbuf.buf_addr.add(mbuf.data_off as _).cast() };
        shared_info.free_cb = Some(packet_free_callback);
        shared_info.fcb_opaque = packet as *mut _ as *mut _;
        shared_info.refcnt = 1;

        let pkt_len = packet.len();
        let buf_addr = packet.data().cast();
        let iova = packet.iova();

        // unsafe { sys::rte_pktmbuf_attach_extbuf(mbuf, buf_addr, iova, pkt_len, shared_info) };
        mbuf.buf_addr = buf_addr;
        unsafe { sys::rte_mbuf_iova_set(mbuf, iova) };
        mbuf.buf_len = pkt_len;
        mbuf.data_off = 0;
        mbuf.ol_flags |= RTE_MBUF_F_EXTERNAL;
        mbuf.shinfo = shared_info;

        // sum of all segments
        mbuf.pkt_len = pkt_len as _;
        // amount of data in segment buffer
        mbuf.data_len = pkt_len;

        // if mbuf was properly inited, then we don't have to set these fields again
        // mbuf.nb_segs = 1;
        // mbuf.next = null_mut();

        let headers = unsafe { &mut mbuf.__bindgen_anon_3.__bindgen_anon_1 };
        headers.set_l2_len(packet.l2_header.length as _);
        headers.set_l3_len(packet.l3_header.length as _);

        let ethh = packet.get_l2_header::<EthHeader>();
        match ethh.ether_ty {
            EtherType::Ipv4 => {
                mbuf.ol_flags |= RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4;

                let ipv4h = packet.get_l3_header::<Ipv4Header>();
                ipv4h.cksum = 0;

                match ipv4h.protocol {
                    Ipv4ProtoType::UDP => {
                        mbuf.ol_flags |= RTE_MBUF_F_TX_UDP_CKSUM;
                        let udph = packet.get_l4_header::<UdpHeader>();
                        udph.chksum = unsafe {
                            sys::rte_ipv4_phdr_cksum(ipv4h as *mut _ as *const _, mbuf.ol_flags)
                        };
                    }
                    _ => {
                        unimplemented!("unsupported l4 type")
                    }
                }
            }
            _ => {
                unimplemented!("unsupported l3 type")
            }
        }

        #[cfg(feature = "perf")]
        {
            self.stats.sent_pkts += 1;
            self.stats.sent_bytes += packet.len() as u64;
        }

        if self.tx_idx == BURST_SIZE {
            self.flush(ctx)?;
        }

        Ok(())
    }

    fn flush(&mut self, _ctx: &Context) -> Result<()> {
        if self.tx_idx == 0 {
            return Ok(());
        }

        let mut pkts = self.tx_bufs.as_mut_ptr();
        let sent = self.tx_idx;

        while self.tx_idx != 0 {
            let nb_tx =
                unsafe { sys::rte_eth_tx_burst(self.port, self.queue, pkts, self.tx_idx as _) };
            pkts = unsafe { pkts.add(nb_tx as usize) };
            self.tx_idx -= nb_tx as usize;
        }

        let ret = unsafe {
            sys::rte_pktmbuf_alloc_bulk(
                self.pktmbuf_pool,
                self.tx_bufs.as_mut_ptr().cast(),
                sent as _,
            )
        };
        if ret != 0 {
            return Err(MemoryError::Exhausted.into());
        }

        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        let rx_items = unsafe {
            sys::rte_eth_rx_burst(
                self.port,
                self.queue,
                self.rx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
            )
        };
        if rx_items == 0 {
            return Err(Error::Nop);
        }

        let mut ret = null_mut();
        let mut i = rx_items as usize;
        while i != 0 {
            i -= 1;

            // TODO: allocate many?
            let pkt = ctx.pktpool.allocate()?;
            let mbuf = unsafe { &mut **self.rx_bufs.get_unchecked(i) };

            pkt.nic = self.nic;
            pkt.offset = mbuf.data_off;
            pkt.set_len(mbuf.pkt_len as _);
            pkt.refcnt = 1;
            // pkt.flow_hash = unsafe { (*mbuf).__bindgen_anon_2.hash.rss };
            pkt.data = mbuf.buf_addr.cast();
            pkt.next = ret;
            pkt.buf_ty = PktBufType::Mbuf(mbuf);
            ret = pkt;

            // TODO: prefetch?

            #[cfg(feature = "perf")]
            {
                self.stats.recv_bytes += mbuf.pkt_len as u64;
            }
        }

        #[cfg(feature = "perf")]
        {
            self.stats.recv_pkts += rx_items as u64;
        }

        Ok(unsafe { &mut *ret })
    }

    fn stop(&self, ctx: &Context) {
        #[cfg(feature = "perf")]
        {
            let elapsed = self.stats.begin.elapsed().as_secs_f64();

            let pps = self.stats.sent_pkts as f64 / elapsed;
            let bps = self.stats.sent_bytes as f64 * 8_f64 / elapsed;
            let gbps = bps / 1_000_000_f64;
            log::info!("[CPU {}] Tx {:.1}pps, {:.1}Mbps", ctx.cpu, pps, gbps);

            let pps = self.stats.recv_pkts as f64 / elapsed;
            let bps = self.stats.recv_bytes as f64 * 8_f64 / elapsed;
            let gbps = bps / 1_000_000_f64;
            log::info!("[CPU {}] Rx {:.1}pps, {:.1}Mbps", ctx.cpu, pps, gbps);
        }
    }
}
