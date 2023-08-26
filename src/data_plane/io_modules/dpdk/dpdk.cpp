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
    constexpr int kMaxNumQueues = 32;
    
    constexpr int kMbufCount = 16384;
    constexpr int kLocalCahe = 512;
    constexpr int kMBufSize = 
        AlignTo(common::kMtu + sizeof(common::EthernetHeader) + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM, 64);
    constexpr int kSendQueueSize = 32;
    constexpr int kRecvQueueSize = 128;
    constexpr int kDefaultRxDescSize = 512;
    constexpr int kDefaultTxDescSize = 512;

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

    class DpdkAdapter;
    class DpdkSendQueue : public io::IoSendQueue {
    public:
        DpdkSendQueue() {}
        virtual ~DpdkSendQueue() {}

        virtual void SendPacket(packet::Packet* packet) override;
        // /** Periodcally called **/
        virtual void FlushSendPacket() override;

        void Init(int port_id, int queue_id, struct rte_mempool* mempool, DpdkAdapter* adapter);

    private:
        struct rte_mbuf* buffer_[kSendQueueSize];
        int index_;
        int port_id_;
        int queue_id_;
        struct rte_mempool* mempool_;
    };

    class DpdkRecvQueue : public io::IoRecvQueue {
    public:
        DpdkRecvQueue() {}
        virtual ~DpdkRecvQueue() {}

        virtual packet::Packet* RecvPacket() override;

        // virtual void RedirectFlow(packet::Packet* packet)  override;

        void Init(int port_id, int queue_id, struct rte_mempool* mempool, DpdkAdapter* adapter, packet::PacketPool* packet_pool);

    private:
        int port_id_;
        int queue_id_;
        struct rte_mbuf* buffer_[kRecvQueueSize];
        int index_;
        int size_;
        struct rte_mempool* mempool_;
        packet::PacketPool* packet_pool_;
    };

    class DpdkAdapter : public io::IoAdapter<DpdkAdapter, kName> {
    public:
        DpdkAdapter() {}
        virtual ~DpdkAdapter() {}

        virtual void InitializeDriver() override;
            
        virtual int AcqurieNumAdapters() override;

        virtual void InitializeAdapter(int port_id, int num_queues) override;

        virtual std::pair<io::IoSendQueue*, io::IoRecvQueue*> InitializeQueue(int queue_id, packet::PacketPool* packet_pool) override;

        virtual void Start() override;
    private:
        packet::PacketPool* packet_pool_;
        int num_queues_;
        int port_id_;

        struct rte_mempool* mempool_;
        struct rte_eth_dev_info dev_info_;
        struct rte_eth_conf queue_conf_;

        uint16_t nb_rx_desc_;
        uint16_t nb_tx_desc_;

        friend class DpdkSendQueue;
        friend class DpdkRecvQueue;
    };

    void DpdkSendQueue::Init(int port_id, int queue_id, struct rte_mempool* mempool, DpdkAdapter* adapter) {
        int current_socket = memory::GetCurrentSocket();
        index_ = 0;
        queue_id_ = queue_id;
        mempool_ = mempool;
        
        struct rte_eth_txconf txq_conf = adapter->dev_info_.default_txconf;
        txq_conf.offloads = adapter->queue_conf_.txmode.offloads;

        auto ret = rte_eth_tx_queue_setup(port_id_, queue_id, 
            adapter->nb_tx_desc_, current_socket, &txq_conf);
        if (ret < 0) throw std::runtime_error("DPDK tx queue setup");

        for (int j = 0; j < kSendQueueSize; j++) {
            buffer_[j] = rte_pktmbuf_alloc(mempool_);
            if (buffer_[j] == NULL) {
                throw std::runtime_error("Failed to allocate mbuf");
            }
        }
    }

    void DpdkRecvQueue::Init(int port_id, int queue_id, struct rte_mempool* mempool, DpdkAdapter* adapter, packet::PacketPool* packet_pool) {
        int current_socket = memory::GetCurrentSocket();
        port_id_ = port_id;
        queue_id_ = queue_id;
        mempool_ = mempool;
        size_ = 0;
        index_ = 0;
        packet_pool_ = packet_pool;
        
        struct rte_eth_rxconf rxq_conf = adapter->dev_info_.default_rxconf;
        rxq_conf.offloads = adapter->queue_conf_.rxmode.offloads;
        rxq_conf.rx_thresh.pthresh = 16;
        rxq_conf.rx_thresh.hthresh = 8;
        rxq_conf.rx_thresh.wthresh = 0;

        auto ret = rte_eth_rx_queue_setup(
            port_id_, queue_id,
            adapter->nb_rx_desc_, current_socket,
            &rxq_conf, mempool_);
        if (ret < 0)
            throw std::runtime_error("DPDK rx queue setup");
    }

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

        nb_rx_desc_ = kDefaultRxDescSize;
        nb_tx_desc_ = kDefaultTxDescSize;
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

    std::pair<io::IoSendQueue*, io::IoRecvQueue*>
        DpdkAdapter::InitializeQueue(int queue_id, packet::PacketPool* packet_pool) {
        packet_pool_ = packet_pool;

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

        auto tx_queue = new DpdkSendQueue();
        tx_queue->Init(port_id_, queue_id, mempool_, this);
        auto rx_queue = new DpdkRecvQueue();
        rx_queue->Init(port_id_, queue_id, mempool_, this, packet_pool);
        return std::make_pair(tx_queue, rx_queue);
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

    static void FreePacket(void* addr, void* packet) {
        reinterpret_cast<packet::Packet*>(packet)->Release();
    }

    static_assert(sizeof(common::EthernetHeader) == 14);
    void DpdkSendQueue::SendPacket(packet::Packet* packet) {
        auto iova_addr = memory::GetIova(packet);
        auto cur_mbuf = buffer_[index_++];
        if (iova_addr != 0) [[likely]] {
            struct rte_mbuf_ext_shared_info* shared_info = rte_pktmbuf_mtod(cur_mbuf, struct rte_mbuf_ext_shared_info*);
            shared_info->free_cb = FreePacket;
            shared_info->fcb_opaque = packet;
            rte_mbuf_ext_refcnt_set(shared_info, 1);
            auto orig_packet = packet;
            if (packet->mbuf_type_ == packet::Packet::MbufType::kIndirect) [[likely]] {
                packet = reinterpret_cast<packet::Packet*>(packet->root_packet_.Get());
            }
            switch (orig_packet->mbuf_type_) {
                case packet::Packet::MbufType::kDpdk: {
                    auto root_mbuf = reinterpret_cast<struct rte_mbuf*>(orig_packet->root_packet_.Get());
                    auto iova = rte_pktmbuf_iova(root_mbuf) + (orig_packet->data_.Get() - rte_pktmbuf_mtod(root_mbuf, char*)) + packet->offset_;
                    rte_pktmbuf_attach_extbuf(cur_mbuf, packet->data_ + packet->offset_,
                        iova, packet->length_ - packet->offset_, shared_info);
                    break;
                }
                case packet::Packet::MbufType::kOrigin: {
                    auto iova = packet->iova_ + (packet->data_.Get() - packet->mbuf_) + packet->offset_;
                    rte_pktmbuf_attach_extbuf(cur_mbuf, packet->data_ + packet->offset_,
                        iova, packet->length_ - packet->offset_, shared_info);
                    break;
                }
                default:
                    throw std::runtime_error("Unknown packet mbuf type");
            }
        }

        cur_mbuf->pkt_len = cur_mbuf->data_len = packet->length_ - packet->offset_;
        cur_mbuf->nb_segs = 1;
        cur_mbuf->next = NULL;

        cur_mbuf->l2_len = sizeof(common::EthernetHeader);
        const auto header_tail = packet->header_tail_ - 1;
        common::EthernetHeader* ethh = (common::EthernetHeader*)packet->GetHeaderPayload(header_tail);

        switch (ethh->type) {
            [[likely]] case ETH_PROTO_TYPE_IPV4: {
                common::Ipv4Header* ipv4h = (common::Ipv4Header*)packet->GetHeaderPayload(header_tail-1);
                cur_mbuf->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4;
                cur_mbuf->l3_len = (ipv4h->ihl << 2);
                ipv4h->chksum = 0;
                switch (ipv4h->proto) {
                    [[likely]] case IP_PROTO_TYPE_TCP: {
                        cur_mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM;
                        auto tcph = (common::TcpHeader*)packet->GetHeaderPayload(header_tail-2);
                        tcph->chksum = rte_ipv4_phdr_cksum((const struct rte_ipv4_hdr*)ipv4h, cur_mbuf->ol_flags);
                        break;
                    }
                    case IP_PROTO_TYPE_UDP: {
                        cur_mbuf->ol_flags |= RTE_MBUF_F_TX_UDP_CKSUM;
                        auto udph = (common::UdpHeader*)packet->GetHeaderPayload(header_tail-2);
                        udph->chksum = rte_ipv4_phdr_cksum((const struct rte_ipv4_hdr*)ipv4h, cur_mbuf->ol_flags);
                        break;
                    }
                }
                break;
            }
            case ETH_PROTO_TYPE_IPV6: {
                cur_mbuf->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV6;
                break;
            }
        }

        if (iova_addr == 0) [[unlikely]] {
            rte_memcpy(rte_pktmbuf_mtod(cur_mbuf, char*), packet->data_ + packet->offset_, cur_mbuf->pkt_len);
        }
        if (index_ == kSendQueueSize) FlushSendPacket();
    }

    void DpdkSendQueue::FlushSendPacket() {
        struct rte_mbuf **pkts = buffer_;
        const int sent = index_;
        do {
            auto ret = rte_eth_tx_burst(port_id_, queue_id_, pkts, index_);
            pkts += ret;
            index_ -= ret;
        } while (index_ > 0);
        for (int i = 0; i < sent; i++) {
            buffer_[i] = rte_pktmbuf_alloc(mempool_);
            if (buffer_[i] == nullptr) [[unlikely]] {
                std::cerr << "Failedt to refill send queue" << std::endl;
                exit(1);
            }
        }
    }

    packet::Packet* DpdkRecvQueue::RecvPacket() {
        if (size_ == index_) [[unlikely]] {
            size_ = rte_eth_rx_burst(port_id_, queue_id_, buffer_, kRecvQueueSize);
            index_ = 0;
            if (size_ == 0) [[unlikely]] {
                return nullptr;
            }
        }
        auto ret = packet_pool_->Allocate();
        auto cur_mbuf = buffer_[index_++];
        ret->mbuf_type_ = packet::Packet::MbufType::kDpdk;
        ret->length_ = cur_mbuf->pkt_len;
        ret->flow_hash_ = cur_mbuf->hash.rss;
        ret->data_.Set(rte_pktmbuf_mtod(cur_mbuf, char*));
        ret->root_packet_.Set(cur_mbuf);
        return ret;
    }
}
#endif
