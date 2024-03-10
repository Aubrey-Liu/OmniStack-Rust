use std::ffi::{c_void, CString};
use std::mem::size_of;
use std::sync::atomic::{AtomicBool, Ordering};

use omnistack_core::packet::PktBufType;
use omnistack_core::prelude::*;

use omnistack_sys as sys;
use omnistack_sys::constants::*;

const fn align(x: usize, n: usize) -> usize {
    assert!(n.is_power_of_two());
    (x + n - 1) / n * n
}

const RX_RING_SIZE: u16 = 1024;
const TX_RING_SIZE: u16 = 1024;

const BURST_SIZE: usize = 64;
const CACHE_SIZE: usize = 256;
const PKTMBUF_SIZE: usize = 8192;
const MBUF_SIZE: usize = align(
    packet::MTU + size_of::<EthHeader>() + sys::RTE_PKTMBUF_HEADROOM as usize,
    64,
);

// NOTE: mut is only for signature, it's fine
static mut RSS_KEY: [u8; 40] = [
    0xd1, 0x81, 0xc6, 0x2c, 0xf7, 0xf4, 0xdb, 0x5b, 0x19, 0x83, 0xa2, 0xfc, 0x94, 0x3e, 0x1a, 0xdb,
    0xd9, 0x38, 0x9e, 0x6b, 0xd1, 0x03, 0x9c, 0x2c, 0xa7, 0x44, 0x99, 0xad, 0x59, 0x3d, 0x56, 0xd9,
    0xf3, 0x25, 0x3c, 0x06, 0x2a, 0xdc, 0x1f, 0xfc,
];

// TODO: Split dpdk into rx and tx queues
#[repr(C)]
#[derive(Debug)]
pub struct Dpdk {
    // Cache `BURST_SIZE` packets before sending a patch
    tx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    tx_buf_items: usize,

    rx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    rx_idx: usize,
    rx_buf_items: usize,

    rx_desc: u16,
    tx_desc: u16,

    mempool: *mut sys::rte_mempool,
    mempool_cache: *mut sys::rte_mempool_cache,

    port: u16,
    queue: u16,
}

impl Dpdk {
    fn new() -> Self {
        unsafe { std::mem::zeroed() }
    }

    /// This function should only be called ONCE.
    fn port_init(&mut self, port: u16, num_queues: u16) -> Result<()> {
        if unsafe { sys::rte_eth_dev_is_valid_port(port) } == 0 {
            panic!("invalid port number {port}");
        }

        let mut dev_info: sys::rte_eth_dev_info = unsafe { std::mem::zeroed() };
        let ret = unsafe { sys::rte_eth_dev_info_get(port, &mut dev_info) };
        if ret != 0 {
            panic!("failed to get port info");
        }

        let mut conf = Self::default_port_conf();
        conf.txmode.offloads &= dev_info.tx_offload_capa;
        conf.rxmode.offloads &= dev_info.rx_offload_capa;

        self.rx_desc = RX_RING_SIZE;
        self.tx_desc = TX_RING_SIZE;
        let ret = unsafe {
            sys::rte_eth_dev_adjust_nb_rx_tx_desc(port, &mut self.rx_desc, &mut self.tx_desc)
        };
        if ret != 0 {
            panic!("failed to set descriptor numbers");
        }

        let ret = unsafe { sys::rte_eth_dev_configure(port, num_queues, num_queues, &conf) };
        if ret != 0 {
            panic!("failed to configurate device");
        }

        INIT_DONE_FLAG.store(true, Ordering::Relaxed);

        Ok(())
    }

    fn default_port_conf() -> sys::rte_eth_conf {
        let mut conf: sys::rte_eth_conf = unsafe { std::mem::zeroed() };
        let rxmode = &mut conf.rxmode;
        rxmode.mq_mode = sys::rte_eth_rx_mq_mode_RTE_ETH_MQ_RX_RSS;
        rxmode.mtu = packet::MTU as _;
        rxmode.max_lro_pkt_size = (packet::MTU + size_of::<EthHeader>()) as _;
        rxmode.offloads = RTE_ETH_RX_OFFLOAD_RSS_HASH | RTE_ETH_RX_OFFLOAD_CHECKSUM;

        let txmode = &mut conf.txmode;
        txmode.mq_mode = sys::rte_eth_tx_mq_mode_RTE_ETH_MQ_TX_NONE;
        txmode.offloads = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM
            | RTE_ETH_TX_OFFLOAD_UDP_CKSUM
            | RTE_ETH_TX_OFFLOAD_TCP_CKSUM
            | RTE_ETH_TX_OFFLOAD_SCTP_CKSUM
            | RTE_ETH_TX_OFFLOAD_TCP_TSO
            | RTE_ETH_TX_OFFLOAD_UDP_TSO;

        let rx_adv_conf = &mut conf.rx_adv_conf;
        rx_adv_conf.rss_conf = sys::rte_eth_rss_conf {
            // NOTE: it's just for C signiture, not really mutable
            rss_key: unsafe { RSS_KEY.as_mut_ptr() },
            rss_key_len: unsafe { RSS_KEY.len() } as _,
            rss_hf: // RTE_ETH_RSS_IPV4 |
                RTE_ETH_RSS_FRAG_IPV4
                | RTE_ETH_RSS_NONFRAG_IPV4_TCP
                | RTE_ETH_RSS_NONFRAG_IPV4_UDP
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

        let mut conf = Self::default_port_conf();
        conf.rxmode.offloads &= dev_info.rx_offload_capa;

        let mut rx_conf = dev_info.default_rxconf;
        rx_conf.offloads = conf.rxmode.offloads;

        rx_conf
    }

    fn default_tx_conf(port: u16) -> sys::rte_eth_txconf {
        let mut dev_info: sys::rte_eth_dev_info = unsafe { std::mem::zeroed() };
        let ret = unsafe { sys::rte_eth_dev_info_get(port, &mut dev_info) };
        if ret != 0 {
            panic!("failed to get port info");
        }

        let mut conf = Self::default_port_conf();
        conf.txmode.offloads &= dev_info.tx_offload_capa;

        let mut tx_conf = dev_info.default_txconf;
        tx_conf.offloads = conf.txmode.offloads;

        tx_conf
    }
}

// TODO: is this approach safe?
#[repr(C)]
struct FreeObject<'a> {
    pktpool: &'a PacketPool,
    thread_id: u16,
    packet: *mut Packet,
}

// Is this dangerous?
unsafe extern "C" fn free_packet(_addr: *mut c_void, obj: *mut c_void) {
    let obj = &*obj.cast::<FreeObject>();

    obj.pktpool.deallocate(obj.packet, obj.thread_id)
}

static INIT_GUARD: std::sync::Once = std::sync::Once::new();
static INIT_DONE_FLAG: AtomicBool = AtomicBool::new(false);

impl IoAdapter for Dpdk {
    // might be called multiple times!
    fn init(&mut self, ctx: &Context, port: u16, num_queues: u16, queue: u16) -> Result<MacAddr> {
        self.port = port;
        self.queue = ctx.worker_id;

        log::debug!("Dpdk init() enters");

        let socket_id = unsafe { sys::numa_node_of_cpu(ctx.cpu as _) };
        let name = CString::new(format!("omni_driver_pool_{}_{}", port, ctx.worker_id)).unwrap();

        assert_eq!(unsafe { sys::rte_eth_dev_socket_id(port) }, socket_id);

        self.mempool = unsafe {
            sys::rte_pktmbuf_pool_create(
                name.as_ptr(),
                PKTMBUF_SIZE as _,
                0,
                0,
                MBUF_SIZE as _,
                socket_id as _,
            )
        };
        self.mempool_cache =
            unsafe { sys::rte_mempool_cache_create(CACHE_SIZE as _, socket_id as _) };

        INIT_GUARD.call_once(|| {
            self.port_init(port, num_queues).unwrap();
        });

        // wait until port is fully initialized
        while !INIT_DONE_FLAG.load(Ordering::Relaxed) {
            std::hint::spin_loop();
        }

        let rx_conf = Self::default_rx_conf(port);
        let ret = unsafe {
            sys::rte_eth_rx_queue_setup(
                port,
                queue,
                self.rx_desc,
                socket_id as _,
                &rx_conf,
                self.mempool,
            )
        };
        if ret != 0 {
            panic!("failed to setup rx queue");
        }
        log::debug!("rx queue {} of port {} is ready", queue, port);

        let tx_conf = Self::default_tx_conf(port);
        let ret = unsafe {
            sys::rte_eth_tx_queue_setup(port, queue, self.tx_desc, socket_id as _, &tx_conf)
        };
        if ret != 0 {
            panic!("failed to setup tx queue");
        }
        log::debug!("tx queue {} of port {} is ready", queue, port);

        let ret = unsafe {
            sys::rte_mempool_generic_get(
                self.mempool,
                self.tx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
                self.mempool_cache,
            )
        };
        if ret != 0 {
            return Err(ModuleError::OutofMemory);
        }

        let mut mac_addr = sys::rte_ether_addr { addr_bytes: [0; 6] };
        let ret = unsafe { sys::rte_eth_macaddr_get(port, &mut mac_addr) };
        let mac_addr = MacAddr::from_bytes(mac_addr.addr_bytes);
        log::debug!("MAC addr of port {}: {:?}", port, mac_addr);

        if ret != 0 {
            panic!("failed to get the MAC address of port {port}");
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

    // TODO: buffer and flush
    fn send(&mut self, ctx: &Context, packet: &mut Packet) -> Result<()> {
        let mbuf = unsafe { self.tx_bufs[self.tx_buf_items].as_mut().unwrap() };
        let shared_info: &mut sys::rte_mbuf_ext_shared_info = unsafe {
            mbuf.buf_addr
                .add(mbuf.data_off as _)
                .cast::<sys::rte_mbuf_ext_shared_info>()
                .as_mut()
                .unwrap()
        };

        let mut fcb_opaque = FreeObject {
            pktpool: ctx.pktpool,
            thread_id: ctx.worker_id,
            packet,
        };

        shared_info.free_cb = Some(free_packet);
        shared_info.fcb_opaque = (&mut fcb_opaque) as *mut _ as *mut _;
        unsafe { sys::rte_mbuf_ext_refcnt_set(shared_info, 1) };

        // TODO: is this slow?
        let iova = unsafe { sys::rte_mem_virt2iova(packet as *mut _ as *const _) };

        unsafe {
            sys::rte_pktmbuf_attach_extbuf(
                mbuf,
                packet.data.wrapping_add(packet.offset as _).cast(),
                iova + packet.data_offset_from_base() as u64,
                packet.len(),
                shared_info,
            )
        };

        mbuf.pkt_len = packet.len() as _;
        mbuf.data_len = packet.len();
        mbuf.nb_segs = 1;
        mbuf.next = std::ptr::null_mut();

        let headers = unsafe { &mut mbuf.__bindgen_anon_3.__bindgen_anon_1 };
        headers.set_l2_len(packet.l2_header.length as _);
        headers.set_l3_len(packet.l3_header.length as _);
        headers.set_l4_len(packet.l4_header.length as _);

        self.tx_buf_items += 1;
        if self.tx_buf_items == BURST_SIZE {
            self.flush(ctx)?;
        }

        Ok(())
    }

    fn flush(&mut self, ctx: &Context) -> Result<()> {
        while self.tx_buf_items > 0 {
            let nb_tx = unsafe {
                sys::rte_eth_tx_burst(
                    self.port,
                    self.queue,
                    self.tx_bufs.as_mut_ptr(),
                    self.tx_buf_items as _,
                )
            };
            self.tx_buf_items -= nb_tx as usize;

            // log::debug!(
            //     "[CPU {}] flush {} packets to port {}",
            //     ctx.cpu,
            //     nb_tx,
            //     self.port
            // );
        }

        let ret = unsafe {
            sys::rte_mempool_generic_get(
                self.mempool,
                self.tx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
                self.mempool_cache,
            )
        };

        // log::debug!(
        //     "[CPU {}] PktPool: {}, MbufPool: {}",
        //     ctx.cpu,
        //     ctx.pktpool.remains(),
        //     unsafe { sys::rte_mempool_avail_count(self.mempool) }
        // );

        if ret != 0 {
            return Err(ModuleError::OutofMemory);
        }

        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&'static mut Packet> {
        // let mut stats: sys::rte_eth_stats = unsafe { std::mem::zeroed() };
        // let mut stats: sys::rte_eth_stats = unsafe { std::mem::zeroed() };
        // unsafe { sys::rte_eth_stats_get(self.port, &mut stats) };

        // printf("  Packets Received: %"PRIu64"\n", stats.q[queue_id].rx_packets);
        // printf("  Packets Dropped: %"PRIu64"\n", stats.q[queue_id].rx_drop);
        //
        // log::debug!(
        //     "out: {}, in: {}, error: {}",
        //     stats.opackets,
        //     stats.ipackets,
        //     stats.ierrors,
        // );

        // unsafe { sys::rte_eth_stats_get(self.port, &mut stats) };

        //     assert!( sys::rte_eth_dev_rx_queue_start(port_id, rx_queue_id) )
        // printf(" - RX queue current state: %s\n", rte_eth_dev_rx_queue_state(port_id, queue_id) ? "Active" : "Inactive");
        // printf(" - RX queue packets received: %" PRIu64 " packets\n", stats.q_ipackets[queue_id]);
        // printf(" - RX queue packets dropped by NIC: %" PRIu64 " packets\n", stats.q_errors[queue_id]);
        // printf(" - RX queue mbuf allocation failures: %" PRIu64 " times\n", stats.rx_nombuf);

        if self.rx_buf_items == self.rx_idx {
            let rx = unsafe {
                sys::rte_eth_rx_burst(
                    self.port,
                    self.queue,
                    self.rx_bufs.as_mut_ptr().cast(),
                    BURST_SIZE as _,
                )
            } as _;

            if rx == 0 {
                return Err(ModuleError::NoData);
            }

            self.rx_buf_items = rx;
            self.rx_idx = 0;

            log::debug!("Received {} packets from port {}", rx, self.port);
        }

        let pkt = ctx.allocate().unwrap();
        let mbuf = self.rx_bufs[self.rx_idx];
        pkt.init_from_mbuf(mbuf);
        pkt.refcnt = 1;
        pkt.buf_ty = PktBufType::Mbuf(mbuf);

        // TODO: prefetch data
        self.rx_idx += 1;

        Ok(pkt)
    }
}

/*
*void print_receive_queue_status(uint16_t port_id, uint16_t queue_id) {
    struct rte_eth_dev_info dev_info;
    struct rte_eth_stats stats;

    // Get device information
    rte_eth_dev_info_get(port_id, &dev_info);

    // Get receive queue statistics
    rte_eth_stats_get(port_id, &stats);

    // Display receive queue status
    printf("Receive Queue Status (Port %u, Queue %u):\n", port_id, queue_id);
    printf(" - Maximum RX packet length: %" PRIu32 " bytes\n", dev_info.rx_desc_lim.max_rx_pkt_len);
    printf(" - RX burst size (max): %" PRIu16 " packets\n", dev_info.rx_desc_lim.nb_max);
    printf(" - RX queue current state: %s\n", rte_eth_dev_rx_queue_state(port_id, queue_id) ? "Active" : "Inactive");
    printf(" - RX queue packets received: %" PRIu64 " packets\n", stats.q_ipackets[queue_id]);
    printf(" - RX queue packets dropped by NIC: %" PRIu64 " packets\n", stats.q_errors[queue_id]);
    printf(" - RX queue mbuf allocation failures: %" PRIu64 " times\n", stats.rx_nombuf);
}
* */
register_adapter!(Dpdk);
