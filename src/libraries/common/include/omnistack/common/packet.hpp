//
// Created by liuhao on 23-6-3.
//

#ifndef OMNISTACK_PACKET_H
#define OMNISTACK_PACKET_H

#include <string>

#include <omnistack/common/constant.hpp>

namespace omnistack::common {

    constexpr uint32_t kPacketMbufHeadroom = 128;
    constexpr uint32_t kPacketMbufSize = ((kMtu + kPacketMbufHeadroom + 63)>>6)<<6;
    constexpr uint32_t kPacketMaxHeaderNum = 4;
    constexpr uint32_t kPacketMaxHeaderLength = 128;

    class Packet {
    public:
        Packet() = delete;
        ~Packet() = delete;

        struct PacketHeader {
            uint8_t length_;
            unsigned char* data_;
        };
        
        uint16_t reference_count_;
        uint16_t length_;           // total length of data in mbuf
        uint16_t channel_;          // target channel
        uint16_t offset_;           // current decoded data offset
        uint16_t nic_;              // id of source or target NIC
        uint16_t mbuf_type_;        // Origin, DPDK
        uint32_t custom_mask_;      // bitmask, module can use for transferring infomation
        uint64_t custom_value_;     // value, module can use for transferring data
        uint8_t header_num_;        // number of headers decoded
        unsigned char* data;        // pointer to packet data
        uint64_t iova;              // IO address for DMA
        uint32_t flow_hash_;
        void* memory_pool_;     
        /* a cache line ends here */

        unsigned char mbuf[kPacketMbufSize];
        PacketHeader packet_headers_[kPacketMaxHeaderNum];
        unsigned char packet_header_data[kPacketMaxHeaderNum * kPacketMaxHeaderLength];
    };

    class PacketPool {
    public:
        PacketPool(std::string_view name_prefix, uint32_t packet_count);
        ~PacketPool();

        Packet* Allocate();
        
        void Free(Packet* packet); 

        Packet* Duplicate(Packet* packet);
    };
}

#endif //OMNISTACK_PACKET_H
