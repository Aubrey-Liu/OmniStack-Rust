//
// Created by liuhao on 23-6-3.
//

#ifndef OMNISTACK_PACKET_HPP
#define OMNISTACK_PACKET_HPP

#include <string>

#include <omnistack/common/constant.hpp>
#include <omnistack/memory/memory.h>

#if defined (OMNIMEM_BACKEND_DPDK)
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#endif

namespace omnistack::packet {
    using namespace omnistack::common;

    /* TODO: explain what's the headroom for */
    constexpr uint32_t kPacketMbufHeadroom = 128;
    constexpr uint32_t kPacketMbufSize = ((kMtu + kPacketMbufHeadroom + 63)>>6)<<6;
    constexpr uint32_t kPacketMaxHeaderNum = 4;
    constexpr uint32_t kPacketMaxHeaderLength = 128;

    struct PacketHeader {
        uint8_t length_;
        char* data_;
    };

    class PacketPool;

    class Packet {
    public:
        Packet();

        enum class MbufType : uint16_t {
            kOrigin,
            kDpdk,
            kIndirect,
        };

        /**
         * @brief release a packet by discounting the reference count, if reference count comes to zero, it will be returned to the packet pool
        */
        inline void Release();

        uint16_t reference_count_;
        uint16_t length_;           // total length of data in mbuf
        uint16_t channel_;          // target channel
        uint16_t offset_;           // current decoded data offset
        uint16_t nic_;              // id of source or target NIC
        MbufType mbuf_type_;        // Origin, DPDK, Indirect
        /* TODO: change this to register mechanism */
        uint32_t custom_mask_;      // bitmask, module can use for transferring infomation
        uint64_t custom_value_;     // value, module can use for transferring data
        PacketHeader* header_tail_; // pointer to decoded headers' tail
        char* data_;                // pointer to packet data
        uint64_t iova_;             // IO address for DMA
        uint32_t flow_hash_;
        uint16_t header_offset_;    // offset of packet_header_data_
        uint16_t padding_;
        Packet* next_packet_;
        /* a cache line ends here */
        PacketPool* packet_pool_;
        uint32_t next_hop_filter_;      /* bitmask presents next hop nodes, if it is set by main logic, corresponding filter will be ignored */
        uint32_t upstream_node_;        /* identify the upstream node of current packet */

        char mbuf_[kPacketMbufSize];
        PacketHeader packet_headers_[kPacketMaxHeaderNum];
        char packet_header_data_[kPacketMaxHeaderNum * kPacketMaxHeaderLength];
    };

    class PacketPool {
    public:
        /**
         * @brief create a packet pool
         * @param name_prefix name prefix of the packet pool
         * @param packet_count number of packets in the pool
         * @return pointer to the packet pool
         * @note the memory subsystem must be initialized before calling this function
        */
        static PacketPool* CreatePacketPool(std::string_view name_prefix, uint32_t packet_count);

        /**
         * @brief destroy a packet pool
         * @param packet_pool pointer to the packet pool
        */
        static void DestroyPacketPool(PacketPool* packet_pool);

        inline Packet* Allocate();

        /**
         * @brief free a packet, the reference count will be ignored
        */
        inline void Free(Packet* packet);

        inline Packet* Duplicate(Packet* packet);
    
        inline Packet* Reference(Packet* packet);

    private:
        PacketPool() = default;
        PacketPool(const PacketPool&) = default;
        PacketPool(PacketPool&&) = default;
        ~PacketPool() = default;

        omnistack::memory::MemoryPool* memory_pool_;
    };

    inline void Packet::Release() {
        if (reference_count_ == 1) [[likely]] {
            switch (mbuf_type_) {
                case MbufType::kOrigin:
                    if (packet_pool_ != nullptr) [[likely]] packet_pool_->Free(this);
                    else delete this;
                    break;
                /* TODO: renew this */
#if defined (OMNIMEM_BACKEND_DPDK)
                case MbufType::kDpdk:
                    rte_pktmbuf_free(reinterpret_cast<rte_mbuf*>(data_ - RTE_PKTMBUF_HEADROOM - sizeof(rte_mbuf)));
                    break;
#endif
                case MbufType::kIndirect: {
                    Packet* packet = reinterpret_cast<Packet*>(data_ - offsetof(Packet, mbuf_));
                    packet->Release();
                    if (packet_pool_ != nullptr) [[likely]] packet_pool_->Free(this);
                    else delete this;
                    break;
                }
                default:
                    /* TODO: report error */
                    break;
            }
        } else reference_count_--;
    }

    inline Packet* PacketPool::Allocate() {
        auto chunk = memory_pool_->Get();
        if(chunk == nullptr) return nullptr;
        auto packet = new(chunk) Packet;
        packet->packet_pool_ = this;
        return packet;
    }

    inline void PacketPool::Free(Packet* packet) {
        printf("free packet\n");
        memory_pool_->Put(packet);
    }

    inline Packet* PacketPool::Duplicate(Packet* packet) {
        auto packet_copy = reinterpret_cast<Packet*>(memory_pool_->Get());
        if(packet_copy == nullptr) return nullptr;
        packet_copy->reference_count_ = 1;
        packet_copy->length_ = packet->length_;
        packet_copy->channel_ = packet->channel_;
        packet_copy->offset_ = packet->offset_;
        packet_copy->nic_ = packet->nic_;
        packet_copy->mbuf_type_ = Packet::MbufType::kOrigin;
        packet_copy->custom_mask_ = packet->custom_mask_;
        packet_copy->custom_value_ = packet->custom_value_;
        packet_copy->header_tail_ = packet_copy->packet_headers_;
        packet_copy->data_ = packet_copy->mbuf_;
        packet_copy->iova_ = packet->iova_;
        packet_copy->next_packet_ = nullptr;
        packet_copy->packet_pool_ = this;
#if defined (OMNIMEM_BACKEND_DPDK)
        rte_memcpy(packet_copy->mbuf_, packet->mbuf_, packet->length_);
#else 
        memcpy(packet_copy->mbuf_, packet->mbuf_, packet->length_);
#endif
        auto packet_header = packet->packet_headers_;
        auto dst_header_data = packet_copy->packet_header_data_;
        while(packet_header != packet->header_tail_) {
            packet_copy->header_tail_->length_ = packet_header->length_;
            packet_copy->header_tail_->data_ = dst_header_data;
#if defined (OMNIMEM_BACKEND_DPDK)
            rte_memcpy(dst_header_data, packet_header->data_, packet_header->length_);
#else
            memcpy(dst_header_data, packet_header->data_, packet_header->length_);
#endif
            dst_header_data += packet_header->length_;
            packet_copy->header_tail_ ++;
            packet_header ++;
        }
        packet_copy->header_offset_ = dst_header_data - packet_copy->packet_header_data_;
        return packet_copy;
    }

    inline Packet* PacketPool::Reference(Packet* packet) {
        auto packet_copy = reinterpret_cast<Packet*>(memory_pool_->Get());
        if(packet_copy == nullptr) return nullptr;
        packet_copy->reference_count_ = 1;
        packet_copy->length_ = packet->length_;
        packet_copy->channel_ = packet->channel_;
        packet_copy->offset_ = packet->offset_;
        packet_copy->nic_ = packet->nic_;
        packet_copy->mbuf_type_ = Packet::MbufType::kIndirect;
        packet_copy->custom_mask_ = packet->custom_mask_;
        packet_copy->custom_value_ = packet->custom_value_;
        packet_copy->header_tail_ = packet_copy->packet_headers_;
        packet_copy->data_ = packet->data_;
        packet_copy->iova_ = packet->iova_;
        packet_copy->next_packet_ = nullptr;
        packet_copy->packet_pool_ = this;
        auto packet_header = packet->packet_headers_;
        while(packet_header != packet->header_tail_) {
            packet_copy->header_tail_->length_ = packet_header->length_;
            packet_copy->header_tail_->data_ = packet_header->data_;
            packet_copy->header_tail_ ++;
            packet_header ++;
        }
        packet->reference_count_ ++;
        return packet_copy;
    }
}

#endif //OMNISTACK_PACKET_HPP
