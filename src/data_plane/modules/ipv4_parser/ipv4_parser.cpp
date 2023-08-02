//
// Created by liuhao on 23-8-1.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

#include <arpa/inet.h>

namespace omnistack::data_plane::ipv4_parser {

using namespace omnistack::common;

inline constexpr char kName[] = "Ipv4Parser";

class Ipv4Parser : public Module<Ipv4Parser, kName> {
public:
    static bool DefaultFilter(DataPlanePacket* packet);

    Filter GetFilter(std::string_view upstream_node, uint32_t global_id) override { return DefaultFilter; };

    DataPlanePacket* MainLogic(DataPlanePacket* packet) override;

    constexpr bool allow_duplication_() override { return true; }

    constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
};

bool Ipv4Parser::DefaultFilter(DataPlanePacket* packet) {
    PacketHeader &eth = packet->packet_headers_[0];
    EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(eth.data_);
    return eth_header->type == ETH_PROTO_TYPE_IPV4;
}

DataPlanePacket* Ipv4Parser::MainLogic(DataPlanePacket* packet) {
    Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->offset_);
    PacketHeader &ipv4 = *(packet->header_tail_ ++);
    ipv4.length_ = ipv4_header->ihl << 2;
    ipv4.data_ = reinterpret_cast<unsigned char*>(ipv4_header);
    packet->length_ = ntohs(ipv4_header->len) + packet->offset_;
    packet->offset_ += ipv4.length_;
    return packet;
}

}
