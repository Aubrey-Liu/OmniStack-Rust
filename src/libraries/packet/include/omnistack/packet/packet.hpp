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
#else
#include <cstring>
#endif

namespace omnistack::node {
    class BasicNode;
}

namespace omnistack::packet {
    using namespace omnistack::common;
    using omnistack::memory::Pointer;

    constexpr uint32_t kPacketMbufHeadroom = 128;   // headroom for mbuf to store packet header
    constexpr uint32_t kPacketMbufSize = ((kMtu + kPacketMbufHeadroom + 63)>>6)<<6;
    constexpr uint32_t kPacketMaxHeaderNum = 4;

    struct PacketHeader {
        uint8_t length_;
        uint8_t offset_;        // offset referred to data_ in packet
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
        Pointer<char> data_;        // pointer to packet data
        uint32_t flow_hash_;
        uint16_t header_tail_;      // index into packet_headers_
        uint16_t padding;
        PacketHeader packet_headers_[kPacketMaxHeaderNum];
        Pointer<Packet> next_packet_;
        Pointer<node::BasicNode> node_;        // pointer to the node which the packet belongs to
        /* a cache line ends here */

        uint64_t iova_; // Warning : Not optimized
        Pointer<void> root_packet_; // dpdk packet use this as pointer to rte_mbuf

        uint32_t next_hop_filter_;      /* bitmask presents next hop nodes, if it is set by main logic, corresponding filter will be ignored */
        uint32_t upstream_node_name_;   /* identify the upstream node of current packet */
        uint32_t upstream_node_id_;

        char mbuf_[kPacketMbufSize];

        inline char* GetHeaderPayload(const int& index) const {
            return data_ + static_cast<uint32_t>(packet_headers_[index].offset_);
        }
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
        static void Free(Packet* packet);

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
                    break;
#if defined (OMNIMEM_BACKEND_DPDK)
                case MbufType::kDpdk:
                    rte_pktmbuf_free(reinterpret_cast<rte_mbuf*>(root_packet_.Get()));
                    // rte_pktmbuf_free(reinterpret_cast<rte_mbuf*>(data_ - RTE_PKTMBUF_HEADROOM - sizeof(rte_mbuf)));
                    break;
#endif
                case MbufType::kIndirect: {
                    auto packet = reinterpret_cast<Packet*>(root_packet_.Get());
                    packet->Release();
                    break;
                }
                default:
                    /* TODO: report error */
                    break;
            }
            PacketPool::Free(this);
        } else reference_count_--;
    }

    inline Packet* PacketPool::Allocate() {
        auto chunk = memory_pool_->Get();
        if(chunk == nullptr) return nullptr;
        auto packet = new(chunk) Packet;
        return packet;
    }

    inline void PacketPool::Free(Packet* packet) {
        printf("free packet\n");
        memory::MemoryPool::PutBack(packet);
    }

    inline Packet* PacketPool::Duplicate(Packet* packet) {
        if(packet->mbuf_type_ == Packet::MbufType::kIndirect) packet = reinterpret_cast<Packet*>(packet->root_packet_.Get());
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
        if(packet->mbuf_type_ == Packet::MbufType::kDpdk) packet_copy->data_ = packet_copy->mbuf_ + kPacketMbufHeadroom;
        else if(packet->mbuf_type_ == Packet::MbufType::kOrigin) packet_copy->data_ = packet_copy->mbuf_ + (packet->data_.Get() - packet->mbuf_);
        packet_copy->next_packet_ = nullptr;
        packet_copy->node_ = packet->node_;
        packet_copy->iova_ = memory::GetIova(packet_copy) + offsetof(Packet, mbuf_) + kPacketMbufHeadroom;
#if defined (OMNIMEM_BACKEND_DPDK)
        rte_memcpy(packet_copy->data_.Get(), packet->data_.Get(), packet->length_);
#else 
        memcpy(packet_copy->data_.Get(), packet->data_.Get(), packet->length_);
#endif
        while(packet_copy->header_tail_ != packet->header_tail_) {
            auto& header = packet_copy->packet_headers_[packet_copy->header_tail_];
            header.length_ = packet->packet_headers_[packet_copy->header_tail_].length_;
            header.offset_ = packet->packet_headers_[packet_copy->header_tail_].offset_;
            packet_copy->header_tail_ ++;
        }
        return packet_copy;
    }

    inline Packet* PacketPool::Reference(Packet* packet) {
        if(packet->mbuf_type_ == Packet::MbufType::kIndirect) packet = reinterpret_cast<Packet*>(packet->root_packet_.Get());
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
        packet_copy->data_ = packet->data_;
        packet_copy->next_packet_ = nullptr;
        packet_copy->node_ = packet->node_;
        packet_copy->iova_ = packet->iova_;
        packet->root_packet_ = reinterpret_cast<void*>(packet);
        while(packet_copy->header_tail_ != packet->header_tail_) {
            auto& header = packet_copy->packet_headers_[packet_copy->header_tail_];
            header.length_ = packet->packet_headers_[packet_copy->header_tail_].length_;
            header.offset_ = packet->packet_headers_[packet_copy->header_tail_].offset_;
            packet_copy->header_tail_ ++;
        }
        packet->reference_count_ ++;
        return packet_copy;
    }
}

#endif //OMNISTACK_PACKET_HPP
