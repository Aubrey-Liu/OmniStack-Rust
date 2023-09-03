#if defined(OMNIDRIVE_EF_VI)
#include <omnistack/io/io_adapter.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/packet/packet.hpp>
#include <numa.h>
#include <stdexcept>

#include <etherfabric/ef_vi.h>
#include <etherfabric/base.h>
#include <etherfabric/pd.h>
#include <etherfabric/vi.h>
#include <etherfabric/memreg.h>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_memcpy.h>
#define MEMCPY rte_memcpy
#else
#define MEMCPY memcpy
#endif

namespace omnistack::io_module::ef_vi {
    constexpr char kName[] = "ef_vi";
    constexpr int kMtu = 1500;
    constexpr int kRecvQueueSize = 64;
    constexpr int kSendQueueSize = 64;
    constexpr int kInRingBuffer = 256;
    constexpr int kHeadroomSize = 128;

    struct EFVIMbuf {
        uint32_t length;
        char payload[0];
    };

    class EFVIAdapter : public io::IoAdapter<EFVIAdapter, kName> {
    public:
        virtual void InitializeDriver() override;

        virtual int AcqurieNumAdapters() override;

        virtual void InitializeAdapter(int port_id, int num_queues) override;

        virtual std::pair<io::IoSendQueue*, io::IoRecvQueue*> InitializeQueue(int queue_id, packet::PacketPool* packet_pool) override;

        virtual void Start() override;

    private:
        inline static ef_driver_handle driver_handle_;
        ef_pd prot_domain_;
        int port_id_;
        int num_queues_;
        
        std::vector<memory::MemoryPool*> memory_pools_;
        std::vector<ef_memreg> memregs_;

        friend class EFVIRecvQueue;
        friend class EFVISendQueue;
    };

    class EFVIRecvQueue : public io::IoRecvQueue {
    public:
        EFVIRecvQueue(int queue_id, packet::PacketPool* packet_pool, EFVIAdapter* adapter,
            uint8_t* mbuf_base, uint64_t mbuf_iova_base);
        virtual ~EFVIRecvQueue() {}

        virtual packet::Packet* RecvPacket() override;
    
    private:
        ::ef_vi vi_;
        int prefix_len_;
        EFVIAdapter* adapter_;
        memory::MemoryPool* mempool_;
        packet::PacketPool* packet_pool_;
        uint8_t* mbuf_base_;
        uint64_t mbuf_iova_base_;

        EFVIMbuf* buffer_[kRecvQueueSize];
        int buffer_size_;
        int buffer_count_;
        EFVIMbuf* in_ring_buffer_[kInRingBuffer];
        int in_ring_buffer_size_;
    };

    class EFVISendQueue : public io::IoSendQueue {
    public:
        EFVISendQueue(int queue_id, packet::PacketPool* packet_pool, EFVIAdapter* adapter,
            uint8_t* mbuf_base, uint64_t mbuf_iova_base);
        virtual ~EFVISendQueue() {}

        virtual void SendPacket(packet::Packet* packet) override;
        virtual void FlushSendPacket() override;
    
    private:
        inline void Poll();

        ::ef_vi vi_;
        packet::PacketPool* packet_pool_;
        memory::MemoryPool* memory_pool_;
        uint8_t* mbuf_base_;
        uint64_t mbuf_iova_base_;

        EFVIMbuf* buffer_[kSendQueueSize];
        int buffer_size_;
        EFVIMbuf* in_ring_buffer_[kInRingBuffer];
        int in_ring_usable_[kInRingBuffer];
        int usable_in_ring_size_;
    };

    EFVIRecvQueue::EFVIRecvQueue(int queue_id, packet::PacketPool* packet_pool, EFVIAdapter* adapter,
        uint8_t* mbuf_base, uint64_t mbuf_iova_base) {
        if (ef_vi_alloc_from_pd(&vi_, adapter->driver_handle_, &adapter->prot_domain_, 
            adapter->driver_handle_, -1, -1, 0, NULL, adapter->driver_handle_, EF_VI_FLAGS_DEFAULT) != 0)
            throw std::runtime_error("Failed to create ef_vi recv queue");
        adapter_ = adapter;
        mempool_ = adapter->memory_pools_[queue_id];
        buffer_size_ = 0;
        buffer_count_ = 0;
        in_ring_buffer_size_ = 0;
        packet_pool_ = packet_pool;
        mbuf_base_ = mbuf_base;
        mbuf_iova_base_ = mbuf_iova_base;
        
        ef_filter_spec filter_spec;
        ef_filter_spec_init(&filter_spec, EF_FILTER_FLAG_NONE);
        ef_filter_spec_set_unicast_all(&filter_spec);
        ef_vi_filter_add(&vi_, adapter->driver_handle_, &filter_spec, nullptr);
        ef_filter_spec_init(&filter_spec, EF_FILTER_FLAG_NONE);
        ef_filter_spec_set_multicast_all(&filter_spec);
        ef_vi_filter_add(&vi_, adapter->driver_handle_, &filter_spec, nullptr);
        prefix_len_ = ef_vi_receive_prefix_len(&vi_);

        for (int i = 0; i < kInRingBuffer; i ++) {
            EFVIMbuf* mbuf = (EFVIMbuf*)mempool_->Get();
            in_ring_buffer_[i] = mbuf;
            in_ring_buffer_size_ ++;
            ef_vi_receive_init(&vi_, ((uint8_t*)mbuf->payload - mbuf_base) + mbuf_iova_base + kHeadroomSize, i);
        }
        ef_vi_receive_push(&vi_);
    }

    packet::Packet* EFVIRecvQueue::RecvPacket() {
        if (buffer_size_ == buffer_count_) [[unlikely]] {
            static thread_local ef_event events[kRecvQueueSize];
            static thread_local int released_ids[kRecvQueueSize];
            static thread_local int released_size;
            released_size = 0;
            int num_events = ef_eventq_poll(&vi_, events, kRecvQueueSize);
            buffer_count_  = 0;
            buffer_size_ = 0;
            for (int i = 0; i < num_events; i ++) {
                auto& event = events[i];
                switch ( EF_EVENT_TYPE(event) ) {
                    case EF_EVENT_TYPE_RX: {
                        int id = EF_EVENT_RX_RQ_ID(event);
                        EFVIMbuf* mbuf = in_ring_buffer_[id];
                        mbuf->length = EF_EVENT_RX_BYTES(event);
                        buffer_[buffer_size_ ++] = mbuf;
                        released_ids[released_size ++] = id;
                        break;
                    }
                    case EF_EVENT_TYPE_RX_DISCARD: {
                        int id = EF_EVENT_RX_RQ_ID(event);
                        EFVIMbuf* mbuf = in_ring_buffer_[id];
                        memory::MemoryPool::PutBack(mbuf);
                        released_ids[released_size ++] = id;
                        break;
                    }
                    default:
                        throw std::runtime_error("Unkonwn type of event");
                }
                in_ring_buffer_size_ --;
            }

            if (released_size) [[likely]] {
                for (int i = 0; i < released_size; i ++) {
                    EFVIMbuf* mbuf = (EFVIMbuf*)mempool_->Get();
                    in_ring_buffer_[released_ids[i]] = mbuf;
                    in_ring_buffer_size_ ++;
                    ef_vi_receive_init(&vi_, ((uint8_t*)mbuf->payload - mbuf_base_) + mbuf_iova_base_ + kHeadroomSize, released_ids[i]);
                }
                ef_vi_receive_push(&vi_);
            }
        }

        if (buffer_count_ < buffer_size_) [[likely]] {
            auto mbuf = buffer_[buffer_count_++];
            auto packet = packet_pool_->Allocate();
            packet->mbuf_type_ = packet::Packet::MbufType::kEFVI;
            packet->SetData(mbuf->payload);
            packet->offset_ = prefix_len_ + kHeadroomSize;
            packet->SetLength(mbuf->length);
            packet->root_packet_.Set(mbuf);
            return packet;
        }

        return nullptr;
    }

    EFVISendQueue::EFVISendQueue(int queue_id, packet::PacketPool* packet_pool, EFVIAdapter* adapter,
            uint8_t* mbuf_base, uint64_t mbuf_iova_base) {
        if (ef_vi_alloc_from_pd(&vi_, adapter->driver_handle_, &adapter->prot_domain_, adapter->driver_handle_,
            -1, 0, -1, NULL, adapter->driver_handle_, EF_VI_FLAGS_DEFAULT) != 0)
            throw std::runtime_error("Failed to create ef_vi send queue");
        packet_pool_ = packet_pool;
        memory_pool_ = adapter->memory_pools_[queue_id];
        mbuf_base_ = mbuf_base;
        mbuf_iova_base_ = mbuf_iova_base;

        usable_in_ring_size_ = kInRingBuffer;
        for (int i = 0; i < kInRingBuffer; i ++)
            in_ring_usable_[i] = i;
    }

    inline void EFVISendQueue::Poll() {
        static thread_local ef_event events[kInRingBuffer];
        static thread_local ef_request_id ids[EF_VI_TRANSMIT_BATCH];
        int num_events = ef_eventq_poll(&vi_, events, kInRingBuffer);
        int num_bundled;
        for (int i = 0; i < num_events; i ++) {
            auto& event = events[i];
            switch (EF_EVENT_TYPE(event)) {
                case EF_EVENT_TYPE_TX: {
                    num_bundled = ef_vi_transmit_unbundle(&vi_, &event, ids);
                    for (int j = 0; j < num_bundled; j ++) {
                        auto mbuf = in_ring_buffer_[ids[j]];
                        memory::MemoryPool::PutBack(mbuf);
                        in_ring_usable_[usable_in_ring_size_ ++] = ids[j];
                    }
                    break;
                }
                default:
                    throw std::runtime_error("Unkonwn EFVI event in send queue");
            }
        }
    }

    void EFVISendQueue::SendPacket(packet::Packet* packet) {
        if (buffer_size_ == kSendQueueSize) [[unlikely]] {
            FlushSendPacket();
            buffer_size_ = 0;
        }
        auto mbuf = (EFVIMbuf*)memory_pool_->Get();
        buffer_[buffer_size_ ++] = mbuf;
        mbuf->length = packet->GetLength();
        MEMCPY(mbuf->payload, packet->GetPayload(), packet->GetLength());
        packet->Release();
    }

    void EFVISendQueue::FlushSendPacket() {
        if (usable_in_ring_size_ < buffer_size_) [[unlikely]] {
            do {
                Poll();
            } while (usable_in_ring_size_ < buffer_size_);
        }
        const int buffer_size = buffer_size_;
        for (int i = 0; i < buffer_size; i ++) {
            auto& mbuf = buffer_[i];
            int new_id = in_ring_usable_[-- usable_in_ring_size_];
            in_ring_buffer_[new_id] = mbuf;
            ef_vi_transmit_init(&vi_, ((uint8_t*)mbuf->payload - mbuf_base_) + mbuf_iova_base_,
                mbuf->length, new_id);
        }
        ef_vi_transmit_push(&vi_);
        buffer_size_ = 0;
    }

    void EFVIAdapter::InitializeDriver() {
        if(ef_driver_open(&driver_handle_) != 0) {
            throw std::runtime_error("Failed to open ef_vi driver");
        }
    }

    int EFVIAdapter::AcqurieNumAdapters() {
        return -1; // efvi
    }

    void EFVIAdapter::InitializeAdapter(int port_id, int num_queues) {
        port_id_ = port_id;
        num_queues_ = num_queues;
        if (ef_pd_alloc(&prot_domain_, driver_handle_, port_id, EF_PD_DEFAULT) != 0)
            throw std::runtime_error("Failed to initialize ef_vi adapter");
        memory_pools_.resize(num_queues_, nullptr);
        memregs_.resize(num_queues);
    }

    std::pair<io::IoSendQueue*, io::IoRecvQueue*>
        EFVIAdapter::InitializeQueue(int queue_id, packet::PacketPool* packet_pool) {
        auto mempool_name = "omni_ef_vi_queue_" + std::to_string(port_id_) + "_" + std::to_string(queue_id);
        auto mempool = memory::AllocateMemoryPool(mempool_name, common::kMtu + kHeadroomSize + sizeof(EFVIMbuf), 16384);
        this->memory_pools_[queue_id] = mempool;
        auto meta = mempool->GetChunkMeta();
        auto region = ef_memreg_alloc(&memregs_[queue_id], driver_handle_, &prot_domain_, driver_handle_,
            (uint8_t*)meta, meta->size);
        auto iova_begin_offset = ef_memreg_dma_addr(&memregs_[queue_id], 0);

        auto send_queue = new EFVISendQueue(queue_id, packet_pool, this, (uint8_t*)meta, iova_begin_offset);
        auto recv_queue = new EFVIRecvQueue(queue_id, packet_pool, this, (uint8_t*)meta, iova_begin_offset);
        return std::make_pair(send_queue, recv_queue);
    }

    void EFVIAdapter::Start() {}
}

#endif