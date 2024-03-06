use std::ffi::{c_void, CString};
use std::mem::size_of;

use omnistack_core::prelude::*;
use omnistack_sys as sys;
use omnistack_sys::constants::*;

const fn align(x: usize, n: usize) -> usize {
    assert!(n.is_power_of_two());
    (x + n - 1) / n * n
}

const RX_RING_SIZE: u16 = 2048;
const TX_RING_SIZE: u16 = 2048;

const BURST_SIZE: usize = 64;
const CACHE_SIZE: usize = 64;
const PKTMBUF_SIZE: usize = 8191;
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

#[repr(C)]
#[derive(Debug)]
pub struct Dpdk {
    // Cache `BURST_SIZE` packets before sending a patch
    tx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    tx_buf_items: usize,

    rx_bufs: [*mut sys::rte_mbuf; BURST_SIZE],
    rx_idx: usize,
    rx_buf_items: usize,

    mempool: *mut sys::rte_mempool,
    mempool_cache: *mut sys::rte_mempool_cache,

    port_id: u16,
    queue_id: u16,
}

impl Dpdk {
    fn new() -> Self {
        unsafe { std::mem::zeroed() }
    }

    /// This function should only be called ONCE.
    fn port_init(&self, port: u16, num_queues: u16) -> Result<()> {
        let rx_rings = num_queues;
        let tx_rings = num_queues;

        if unsafe { sys::rte_eth_dev_is_valid_port(port) } == 0 {
            // TODO: log error or add some new error types
            return Err(Error::DpdkInitErr);
        }

        let mut dev_info: sys::rte_eth_dev_info = unsafe { std::mem::zeroed() };
        let ret = unsafe { sys::rte_eth_dev_info_get(port, &mut dev_info) };

        if ret != 0 {
            return Err(Error::DpdkInitErr);
        }

        let mut conf = unsafe { Self::default_port_conf() };
        conf.txmode.offloads &= dev_info.tx_offload_capa;
        conf.rxmode.offloads &= dev_info.rx_offload_capa;

        let mut nb_rx_desc = RX_RING_SIZE;
        let mut nb_tx_desc = TX_RING_SIZE;
        let ret = unsafe {
            sys::rte_eth_dev_adjust_nb_rx_tx_desc(port, &mut nb_rx_desc, &mut nb_tx_desc)
        };
        if ret != 0 {
            return Err(Error::DpdkInitErr);
        }

        let ret = unsafe { sys::rte_eth_dev_configure(port, rx_rings, tx_rings, &conf) };
        if ret != 0 {
            return Err(Error::DpdkInitErr);
        }

        let mut rx_conf = dev_info.default_rxconf;
        rx_conf.offloads = conf.rxmode.offloads;

        for q in 0..rx_rings {
            let ret = unsafe {
                sys::rte_eth_rx_queue_setup(
                    port,
                    q,
                    nb_rx_desc,
                    sys::rte_eth_dev_socket_id(port) as _,
                    &rx_conf,
                    self.mempool,
                )
            };

            if ret != 0 {
                return Err(Error::DpdkInitErr);
            }
        }

        let mut tx_conf = dev_info.default_txconf;
        tx_conf.offloads = conf.txmode.offloads;

        for q in 0..tx_rings {
            let ret = unsafe {
                sys::rte_eth_tx_queue_setup(
                    port,
                    q,
                    nb_rx_desc,
                    sys::rte_eth_dev_socket_id(port) as _,
                    &tx_conf,
                )
            };

            // TODO: Allocate mbuf for tx queues
            if ret != 0 {
                return Err(Error::DpdkInitErr);
            }
        }

        let ret = unsafe { sys::rte_eth_dev_start(port) };
        if ret != 0 {
            return Err(Error::DpdkInitErr);
        }

        let ret = unsafe { sys::rte_eth_promiscuous_enable(port) };

        if ret != 0 {
            return Err(Error::DpdkInitErr);
        }

        log::info!("Port {port} was initialized successfully.");

        Ok(())
    }

    unsafe fn default_port_conf() -> sys::rte_eth_conf {
        let mut conf: sys::rte_eth_conf = std::mem::zeroed();
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
            rss_key: RSS_KEY.as_mut_ptr(),
            rss_key_len: RSS_KEY.len() as _,
            rss_hf: // RTE_ETH_RSS_IPV4 |
                RTE_ETH_RSS_FRAG_IPV4
                | RTE_ETH_RSS_NONFRAG_IPV4_TCP
                | RTE_ETH_RSS_NONFRAG_IPV4_UDP
                // | RTE_ETH_RSS_TCP
                // | RTE_ETH_RSS_UDP,
        };

        conf
    }
}

struct FreeObject<'a> {
    ctx: &'a Context,
    packet: *mut Packet,
}

#[allow(unused_variables)]
unsafe extern "C" fn free_packet(addr: *mut c_void, obj: *mut c_void) {
    let obj = &*obj.cast::<FreeObject>();

    obj.ctx.deallocate(obj.packet);
}

static INIT_GUARD: std::sync::Once = std::sync::Once::new();

impl IoAdapter for Dpdk {
    // might be called multiple times!
    fn init(&mut self, ctx: &Context, port: u16, num_queues: u16) -> Result<MacAddr> {
        self.port_id = port;
        self.queue_id = ctx.graph_id;

        let socket_id = unsafe { sys::numa_node_of_cpu(ctx.core_id as _) };
        let name = CString::new(format!("omni-driver-pool-{port}")).unwrap();

        assert_eq!(unsafe { sys::rte_eth_dev_socket_id(port) }, socket_id);

        self.mempool = unsafe {
            sys::rte_pktmbuf_pool_create(
                name.as_ptr(),
                PKTMBUF_SIZE as _,
                0,
                0,
                MBUF_SIZE as _,
                socket_id,
            )
        };
        self.mempool_cache = unsafe { sys::rte_mempool_cache_create(CACHE_SIZE as _, socket_id) };

        INIT_GUARD.call_once(|| {
            self.port_init(port, num_queues).unwrap();
        });

        let ret = unsafe {
            sys::rte_mempool_generic_get(
                self.mempool,
                self.tx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
                self.mempool_cache,
            )
        };

        if ret != 0 {
            return Err(Error::Unknown);
        }

        let mut mac_addr = sys::rte_ether_addr { addr_bytes: [0; 6] };
        let ret = unsafe { sys::rte_eth_macaddr_get(port, &mut mac_addr) };

        log::info!(
            "MAC addr of port {}: {:?}",
            port,
            MacAddr::from_bytes(mac_addr.addr_bytes)
        );

        if ret != 0 {
            Err(Error::Unknown)
        } else {
            Ok(MacAddr::from_bytes(mac_addr.addr_bytes))
        }
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

        let mut fcb_opaque = FreeObject { ctx, packet };
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

    fn flush(&mut self, _ctx: &Context) -> Result<()> {
        while self.tx_buf_items > 0 {
            let nb_tx = unsafe {
                sys::rte_eth_tx_burst(
                    self.port_id,
                    self.queue_id,
                    self.tx_bufs.as_mut_ptr(),
                    self.tx_buf_items as _,
                )
            };

            self.tx_buf_items -= nb_tx as usize;
        }

        let ret = unsafe {
            sys::rte_mempool_generic_get(
                self.mempool,
                self.tx_bufs.as_mut_ptr().cast(),
                BURST_SIZE as _,
                self.mempool_cache,
            )
        };

        if ret != 0 {
            return Err(Error::Unknown);
        }

        Ok(())
    }

    fn recv(&mut self, ctx: &Context) -> Result<&mut Packet> {
        // TODO: Recieve multiple

        if self.rx_buf_items == self.rx_idx {
            let rx = unsafe {
                sys::rte_eth_rx_burst(
                    self.port_id,
                    self.queue_id,
                    self.rx_bufs.as_mut_ptr().cast(),
                    BURST_SIZE as _,
                ) as _
            };

            self.rx_buf_items = rx;
            self.rx_idx = 0;

            if rx > 0 {
                log::debug!("Received {rx} packets from the NIC");
            } else {
                return Err(Error::NoData);
            }
        }

        let pkt = ctx.allocate().unwrap();
        let mbuf = self.rx_bufs[self.rx_idx];
        pkt.init_from_mbuf(mbuf);
        pkt.port = self.port_id;

        // TODO: prefetch data
        self.rx_idx += 1;

        Ok(pkt)
    }
}

register_adapter!(Dpdk);
