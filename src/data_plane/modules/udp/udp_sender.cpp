//
// Created by zengqihuan on 2023.8.24
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/node.h>


namespace omnistack::data_plane::udp_senders {
    
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "UdpSender";

    class UdpSender : public Module<UdpSender, kName> {
    public:
        UdpSender() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool UdpSender::DefaultFilter(Packet* packet) {
        // TODO: may add some rules here
        return true;
    }

    Packet* UdpSender::MainLogic(Packet* packet) {
        // edit omnistack header
        PacketHeader &udp = packet->packet_headers_[packet->header_tail_ ++];
        udp.length_ = sizeof(UdpHeader);
        udp.offset_ = packet->offset_ - sizeof(UdpHeader);
        UdpHeader* udp_header = reinterpret_cast<UdpHeader*>(packet->data_ + udp.offset_);
        packet->offset_ -= udp.length_;
        packet->length_ += sizeof(UdpHeader);

        // edit udp header, assume port are big-edian
        uint16_t src_port = (*packet->node_).info_.transport.udp.sport;
        uint16_t dst_port = (*packet->node_).info_.transport.udp.dport;
        udp_header->sport = src_port;
        udp_header->dport = dst_port;
        udp_header->chksum = 0;
        udp_header->len = htons(packet->length_);
        return packet;
    }

}