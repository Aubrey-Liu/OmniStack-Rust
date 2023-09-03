//
// Created by zengqihuan on 2023.8.24
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/node.h>
#include <omnistack/common/logger.h>


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
        PacketHeader &udp = packet->l4_header;
        udp.length_ = sizeof(UdpHeader);
        packet->offset_ -= sizeof(UdpHeader);
        udp.offset_ = packet->offset_;
        auto udp_header = packet->GetL4Header<UdpHeader>();

        const auto& daddr = packet->peer_addr_;

        // edit udp header, assume port are big-edian
        uint16_t src_port = (*packet->node_).info_.transport.udp.sport;
        uint16_t dst_port = daddr.sin_addr.s_addr;
        udp_header->sport = src_port;
        udp_header->dport = daddr.sin_port;
        udp_header->len = htons(packet->GetLength());
        return packet;
    }

}