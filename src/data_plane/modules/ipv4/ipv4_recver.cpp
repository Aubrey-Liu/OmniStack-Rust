//
// Created by zengqihuan on 2023-8-19
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>

namespace omnistack::data_plane::ipv4_recver {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "Ipv4Recver";

    class Ipv4Recver : public Module<Ipv4Recver, kName> {
    public:
        Ipv4Recver() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    inline void LogIpv4Address(const char* message, uint32_t ip) {
        printf("%s%d.%d.%d.%d\n", message, ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }

    bool Ipv4Recver::DefaultFilter(Packet* packet) {
        auto eth_header = packet->GetL2Header<EthernetHeader>();
        return eth_header->type == ETH_PROTO_TYPE_IPV4;
    }

    Packet* Ipv4Recver::MainLogic(Packet* packet) {
        // record the input packet's header and update it's length
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->offset_);
        auto& ipv4 = packet->l3_header;
        ipv4.length_ = ipv4_header->ihl << 2;
        ipv4.offset_ = packet->offset_;
        packet->length_ = ntohs(ipv4_header->len) + packet->offset_;
        packet->offset_ += ipv4.length_;

        // check if the packet is invalid
        if (ipv4_header->ihl < 5 || ipv4_header->ttl == 0) [[unlikely]] return nullptr; // drop packet

        // due to NIC offload and upper modules, we won't collect frags or check chksum here.

        return packet;
    }

}
