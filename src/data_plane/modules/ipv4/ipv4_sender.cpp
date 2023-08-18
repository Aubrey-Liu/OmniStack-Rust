//
// Created by liuhao on 23-8-1.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::ipv4_sender {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "Ipv4Sender";

    class Ipv4Sender : public Module<Ipv4Sender, kName> {
    public:
        Ipv4Sender() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    inline void LogIpv4Address(const char* message, uint32_t ip) {
        printf("%s%d.%d.%d.%d\n", message, ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }

    bool Ipv4Sender::DefaultFilter(Packet* packet) {
        auto& eth = packet->packet_headers_[0];
        EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(packet->data_ + eth.offset_);
        return eth_header->type == ETH_PROTO_TYPE_IPV4;
    }

    Packet* Ipv4Sender::MainLogic(Packet* packet) {
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->offset_);
        auto& ipv4 = packet->packet_headers_[packet->header_tail_ ++];
        ipv4.length_ = ipv4_header->ihl << 2;
        ipv4.offset_ = packet->offset_;
        packet->length_ = ntohs(ipv4_header->len) + packet->offset_;
        packet->offset_ += ipv4.length_;

        /* TODO: define a set of log helper */
#ifdef OMNI_DEBUG
        LogIpv4Address("[Ipv4Sender] src = ", ipv4_header->src);
        LogIpv4Address("[Ipv4Sender] dst = ", ipv4_header->dst);
#endif

        return packet;
    }

}
