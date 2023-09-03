//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::tcp_parser {
        
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "TcpParser";

    class TcpParser : public Module<TcpParser, kName> {
    public:
        TcpParser() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool TcpParser::DefaultFilter(Packet* packet) {
        Ipv4Header* ipv4_header = packet->GetL3Header<Ipv4Header>();
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_TCP;
        Ipv6Header* ipv6_header = packet->GetL3Header<Ipv6Header>();
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_TCP;
        return false;
    }

    Packet* TcpParser::MainLogic(Packet* packet) {
        auto tcp_header = packet->GetPayloadType<TcpHeader>();
        PacketHeader &tcp = packet->l4_header;
        tcp.length_ = tcp_header->dataofs << 2;
        tcp.offset_ = packet->offset_;
        packet->offset_ += tcp.length_;
        return packet;
    }

}