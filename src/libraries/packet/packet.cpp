//
// Created by Jeremy Guo on 2023/6/9.
//

#include <omnistack/packet/packet.hpp>

namespace omnistack::packet {

    Packet::Packet() {
        reference_count_ = 1;
        length_ = 0;
        offset_ = 0;
        mbuf_type_ = MbufType::kOrigin;
        custom_mask_ = 0;
        custom_value_ = 0;
        header_tail_ = packet_headers_;
        data_ = mbuf_;
        /* TODO: set iova */
        iova_ = 0;
        header_offset_ = 0;
        next_packet_ = nullptr;
        packet_pool_ = nullptr;
    }

    PacketPool* PacketPool::CreatePacketPool(std::string_view name_prefix, uint32_t packet_count) {
        auto packet_pool = new PacketPool();
        std::string name(name_prefix.data());
        name += "_PacketPool";
        packet_pool->memory_pool_ = omnistack::memory::AllocateMemoryPool(name.data(), sizeof(Packet), packet_count);
        return packet_pool;
    }

    void PacketPool::DestroyPacketPool(PacketPool* packet_pool) {
        omnistack::memory::FreeMemoryPool(packet_pool->memory_pool_);
        delete packet_pool;
    }

}