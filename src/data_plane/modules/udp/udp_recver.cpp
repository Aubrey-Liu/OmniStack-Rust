//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>

namespace omnistack::data_plane::udp_recvers {
    
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "UdpRecver";

    class UdpRecver : public Module<UdpRecver, kName> {
    public:
        UdpRecver() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool UdpRecver::DefaultFilter(Packet* packet) {
        auto& ip = packet->packet_headers_[packet->header_tail_ - 1];
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + ip.offset_);
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_UDP;
        Ipv6Header* ipv6_header = reinterpret_cast<Ipv6Header*>(packet->data_ + ip.offset_);
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_UDP;
        return false;
    }

    Packet* UdpRecver::MainLogic(Packet* packet) {
        // OMNI_LOG_TAG(kDebug, "UdpRecver") << "receive udp packet, length = " << packet->length_ << "\n";
        UdpHeader* udp_header = reinterpret_cast<UdpHeader*>(packet->data_ + packet->offset_);
        PacketHeader &udp = packet->packet_headers_[packet->header_tail_ ++];
        udp.length_ = sizeof(UdpHeader);
        udp.offset_ = packet->offset_;
        packet->offset_ += udp.length_;
        return packet;
    }

}