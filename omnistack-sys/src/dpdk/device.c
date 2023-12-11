#include <rte_ethdev.h>
#include <rte_mempool.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define BURST_SIZE 8

#define MTU 1500

static uint8_t default_rsskey_40bytes[40] = {
    0xd1, 0x81, 0xc6, 0x2c, 0xf7, 0xf4, 0xdb, 0x5b, 0x19, 0x83,
    0xa2, 0xfc, 0x94, 0x3e, 0x1a, 0xdb, 0xd9, 0x38, 0x9e, 0x6b,
    0xd1, 0x03, 0x9c, 0x2c, 0xa7, 0x44, 0x99, 0xad, 0x59, 0x3d,
    0x56, 0xd9, 0xf3, 0x25, 0x3c, 0x06, 0x2a, 0xdc, 0x1f, 0xfc};

const struct rte_eth_conf port_conf_default = {
    .rxmode =
        {
            .mq_mode = ETH_MQ_RX_RSS,
            .mtu = MTU,
            .max_lro_pkt_size = MTU + sizeof(struct rte_ether_hdr),
            .split_hdr_size = 0,
            .offloads = DEV_RX_OFFLOAD_RSS_HASH | DEV_RX_OFFLOAD_CHECKSUM
            //| DEV_RX_OFFLOAD_TCP_LRO
        },
    .txmode =
        {
            .mq_mode = ETH_MQ_TX_NONE,
            .offloads = DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_UDP_CKSUM |
                        DEV_TX_OFFLOAD_TCP_CKSUM | DEV_TX_OFFLOAD_SCTP_CKSUM |
                        DEV_TX_OFFLOAD_TCP_TSO | DEV_TX_OFFLOAD_UDP_TSO,
        },
    .rx_adv_conf = {.rss_conf = {.rss_key = default_rsskey_40bytes,
                                 .rss_key_len = sizeof(default_rsskey_40bytes),
                                 .rss_hf = ETH_RSS_FRAG_IPV4 |
                                           ETH_RSS_NONFRAG_IPV4_UDP |
                                           ETH_RSS_NONFRAG_IPV4_TCP}},
};

int dev_port_init(uint16_t port, uint16_t num_queues,
                  struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = num_queues, tx_rings = num_queues;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port)) return -1;

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        printf("Error during getting device (port %u) info: %s\n", port,
               strerror(-retval));
        return retval;
    }

    port_conf.txmode.offloads &= dev_info.tx_offload_capa;
    port_conf.rxmode.offloads &= dev_info.rx_offload_capa;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0) return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) return retval;

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(
            port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0) return retval;
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;

    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                        rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0) return retval;
    }

    /* Starting Ethernet port. 8< */
    retval = rte_eth_dev_start(port);
    /* >8 End of starting of ethernet port. */
    if (retval < 0) return retval;

    /* Enable RX in promiscuous mode for the Ethernet device. */
    retval = rte_eth_promiscuous_enable(port);
    /* End of setting RX port in promiscuous mode. */
    if (retval != 0) return retval;

    return 0;
}

int dev_macaddr_get(uint16_t port, struct rte_ether_addr *addr) {
    return rte_eth_macaddr_get(port, addr);
}

int dev_send_packet(uint16_t port, uint16_t queue, struct rte_mbuf *tx_bufs[],
                    uint16_t n) {
    uint16_t nb_tx = rte_eth_tx_burst(port, queue, tx_bufs, n);

    return nb_tx;
}

int dev_recv_packet(uint16_t port, uint16_t queue, struct rte_mbuf *rx_bufs[],
                    uint16_t n) {
    uint16_t nb_rx = rte_eth_rx_burst(port, queue, rx_bufs, n);

    return nb_rx;
}
