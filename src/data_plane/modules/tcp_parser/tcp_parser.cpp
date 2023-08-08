//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::tcp_parser {
        
    using namespace omnistack::common;

    inline constexpr char kName[] = "TcpParser";

    class TcpParser : public Module<TcpParser, kName> {
    public:
        TcpParser() {}

        static bool DefaultFilter(DataPlanePacket* packet);

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override { return DefaultFilter; }

        DataPlanePacket* MainLogic(DataPlanePacket* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool TcpParser::DefaultFilter(DataPlanePacket* packet) {
        PacketHeader &ip = *(packet->header_tail_ - 1);
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(ip.data_);
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_TCP;
        Ipv6Header* ipv6_header = reinterpret_cast<Ipv6Header*>(ip.data_);
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_TCP;
        return false;
    }

    DataPlanePacket* TcpParser::MainLogic(DataPlanePacket* packet) {
        TcpHeader* tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + packet->offset_);
        PacketHeader &tcp = *(packet->header_tail_ ++);
        tcp.length_ = tcp_header->dataofs << 2;
        tcp.data_ = reinterpret_cast<unsigned char*>(tcp_header);
        packet->offset_ += tcp.length_;
        return packet;
    }

}