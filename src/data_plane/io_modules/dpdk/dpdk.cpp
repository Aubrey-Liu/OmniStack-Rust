#if defined(OMNIMEM_BACKEND_DPDK)
#include <omnistack/io/io_adapter.hpp>
#include <omnistack/common/protocol_headers.hpp>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <numa.h>

namespace omnistack::io_module::dpdk {
    consteval int AlignTo(int v, int align) {
        return (v + align - 1) / align * align;
    }

    constexpr int kMtu = 1500;
    
    constexpr int kMbufCount = 16384;
    constexpr int kLocalCahe = 512;
    constexpr int kMBufSize = 
        AlignTo(common::kMtu + sizeof(common::EthernetHeader) + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM, 64);
    constexpr int kSendQueueSize = 32;

    constexpr char kName[] = "Dpdk";

    static uint8_t default_rsskey_40bytes[40] = {
        0xd1, 0x81, 0xc6, 0x2c, 0xf7, 0xf4, 0xdb, 0x5b,
        0x19, 0x83, 0xa2, 0xfc, 0x94, 0x3e, 0x1a, 0xdb,
        0xd9, 0x38, 0x9e, 0x6b, 0xd1, 0x03, 0x9c, 0x2c,
        0xa7, 0x44, 0x99, 0xad, 0x59, 0x3d, 0x56, 0xd9,
        0xf3, 0x25, 0x3c, 0x06, 0x2a, 0xdc, 0x1f, 0xfc
    };

    const struct rte_eth_conf port_conf_default = {
        .rxmode = {
            .mq_mode = ETH_MQ_RX_RSS,
            .mtu = kMtu,
            .max_lro_pkt_size = common::kMtu + sizeof(common::EthernetHeader),
            .split_hdr_size = 0,
            .offloads = DEV_RX_OFFLOAD_RSS_HASH | 
                DEV_RX_OFFLOAD_CHECKSUM |
                DEV_RX_OFFLOAD_TCP_LRO
        },
        .txmode = {
            .mq_mode = ETH_MQ_TX_NONE,
            .offloads =
                DEV_TX_OFFLOAD_IPV4_CKSUM  |
                DEV_TX_OFFLOAD_UDP_CKSUM   |
                DEV_TX_OFFLOAD_TCP_CKSUM   |
                DEV_TX_OFFLOAD_SCTP_CKSUM  |
                DEV_TX_OFFLOAD_TCP_TSO |
                DEV_TX_OFFLOAD_UDP_TSO,
        },
        .rx_adv_conf = {
            .rss_conf = {
                .rss_key = default_rsskey_40bytes,
                .rss_key_len = sizeof(default_rsskey_40bytes),
                .rss_hf = 
                    ETH_RSS_FRAG_IPV4           |
                    ETH_RSS_NONFRAG_IPV4_UDP    |
                    ETH_RSS_NONFRAG_IPV4_TCP    |
                    ETH_RSS_TCP
            }
        },
    };

    static_assert(common::kMtu + sizeof(common::EthernetHeader) >= kMtu + sizeof(common::EthernetHeader));

    class DpdkAdapter : public io::IoAdapter<DpdkAdapter, kName> {
    public:
        DpdkAdapter() {}
        virtual ~DpdkAdapter() {}

        virtual void InitializeDriver() override;
            
        virtual int AcqurieNumAdapters() override;

        virtual void InitializeAdapter(int port_id, int num_queues) override;

        virtual void InitializeQueue(int queue_id, packet::PacketPool* packet_pool) override;

        // virtual void SendPacket(int queue_id, packet::Packet* packet) override;
        // /** Periodcally called **/
        // virtual void FlushSendPacket(int queue_id) override;

        virtual void Start() override;

        // virtual packet::Packet* RecvPackets(int queue_id) override;

        // virtual void RedirectFlow(packet::Packet* packet)  override;
    private:
        packet::PacketPool* packet_pool_;
        int num_queues_;
        int port_id_;

        struct rte_mempool* mempool_;
        struct rte_eth_dev_info dev_info_;
        struct rte_eth_conf queue_conf_;

        uint16_t nb_rx_desc_;
        uint16_t nb_tx_desc_;

        struct rte_mbuf* send_queue_[kSendQueueSize];
    };

    void DpdkAdapter::InitializeDriver() {}
    
    int DpdkAdapter::AcqurieNumAdapters() { return -1; }

    void DpdkAdapter::InitializeAdapter(int port_id, int num_queues) {
        num_queues_ = num_queues;
        port_id_ = port_id;

        {
            auto ret = rte_eth_dev_info_get(port_id_, &dev_info_);
            if (ret) throw std::runtime_error("Faield to get device info");
        }

        queue_conf_ = port_conf_default;
        queue_conf_.txmode.offloads &= dev_info_.tx_offload_capa;
        queue_conf_.rxmode.offloads &= dev_info_.rx_offload_capa;

        nb_rx_desc_ = 512;
        nb_tx_desc_ = 512;
        {
            auto ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id_, &nb_rx_desc_, &nb_tx_desc_);
            if (ret) throw std::runtime_error("Failed to adjust number of descriptors");
        }
        {
            auto ret = rte_eth_dev_configure(port_id_, num_queues, num_queues, &queue_conf_);
            if (ret) throw std::runtime_error("Failed to configure device");
        }
        {
            auto ret = rte_flow_flush(port_id_, nullptr);
            if (ret) throw std::runtime_error("Failed to flush flow");
        }
    }

    void DpdkAdapter::InitializeQueue(int queue_id, packet::PacketPool* packet_pool) {
        packet_pool_ = packet_pool;

        int current_socket = memory::GetCurrentSocket();

        {
            char name[RTE_MEMPOOL_NAMESIZE];
            sprintf(name, "omni_driver_pool-%d-%d", port_id_, queue_id);
            mempool_ = rte_mempool_create(
                name, kMbufCount, kMBufSize, kLocalCahe,
                sizeof(struct rte_pktmbuf_pool_private),
                rte_pktmbuf_pool_init, NULL,
                rte_pktmbuf_init, NULL,
                rte_socket_id(), 0);
        }

        {
            struct rte_eth_txconf txq_conf = dev_info_.default_txconf;
            txq_conf.offloads = queue_conf_.txmode.offloads;
            auto ret = rte_eth_tx_queue_setup(port_id_, queue_id, 
                nb_tx_desc_, current_socket, &txq_conf);
            if (ret < 0) throw std::runtime_error("DPDK tx queue setup");
            for (int j = 0; j < kSendQueueSize; j++) {
                send_queue_[j] = rte_pktmbuf_alloc(mempool_);
                if (send_queue_[j] == NULL) {
                    throw std::runtime_error("Failed to allocate mbuf");
                }
            }
        }

        {
            struct rte_eth_rxconf rxq_conf = dev_info_.default_rxconf;
            rxq_conf.offloads = queue_conf_.rxmode.offloads;
            rxq_conf.rx_thresh.pthresh = 16;
            rxq_conf.rx_thresh.hthresh = 8;
            rxq_conf.rx_thresh.wthresh = 0;
            {
                auto ret = rte_eth_rx_queue_setup(
                    port_id_, queue_id,
                    nb_rx_desc_, current_socket,
                    &rxq_conf, mempool_);
                if (ret < 0)
                    throw std::runtime_error("DPDK rx queue setup");
            }
        }
    }

    void DpdkAdapter::Start() {
        {
            auto ret = rte_eth_promiscuous_enable(port_id_);
            if (ret) throw std::runtime_error("Failed to enable promiscuous mode");
        }

        {
            struct rte_eth_fc_conf fc_conf;
            memset(&fc_conf, 0, sizeof(fc_conf));
            {
                auto ret = rte_eth_dev_flow_ctrl_get(port_id_, &fc_conf);
                if (ret) throw std::runtime_error("Failed to get flow control info");
            }
            fc_conf.mode = RTE_FC_NONE;
            {
                auto ret = rte_eth_dev_flow_ctrl_set(port_id_, &fc_conf);
                if (ret) throw std::runtime_error("Failed to set flow control info");
            }
        }
        
        {
            auto ret = rte_eth_dev_start(port_id_);
            if (ret) throw std::runtime_error("Failed to start port");
        }
    }
}
#endif
