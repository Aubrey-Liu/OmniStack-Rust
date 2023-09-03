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
        Ipv4Header* ipv4_header = packet->GetL3Header<Ipv4Header>();
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_UDP;
        Ipv6Header* ipv6_header = packet->GetL3Header<Ipv6Header>();
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_UDP;
        return false;
    }

    Packet* UdpRecver::MainLogic(Packet* packet) {
        auto udp_header = packet->GetPayloadType<UdpHeader>();
        PacketHeader &udp = packet->l4_header;
        udp.length_ = sizeof(UdpHeader);
        udp.offset_ = packet->offset_;
        packet->offset_ += udp.length_;
        return packet;
    }

}