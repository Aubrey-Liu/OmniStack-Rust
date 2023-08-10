//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::udp_parser {
    
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "UdpParser";

    class UdpParser : public Module<UdpParser, kName> {
    public:
        UdpParser() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool UdpParser::DefaultFilter(Packet* packet) {
        PacketHeader &ip = *(packet->header_tail_ - 1);
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(ip.data_);
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_UDP;
        Ipv6Header* ipv6_header = reinterpret_cast<Ipv6Header*>(ip.data_);
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_UDP;
        return false;
    }

    Packet* UdpParser::MainLogic(Packet* packet) {
        UdpHeader* udp_header = reinterpret_cast<UdpHeader*>(packet->data_ + packet->offset_);
        PacketHeader &udp = *(packet->header_tail_ ++);
        udp.length_ = sizeof(UdpHeader);
        udp.data_ = reinterpret_cast<char*>(udp_header);
        packet->offset_ += udp.length_;
        return packet;
    }

}