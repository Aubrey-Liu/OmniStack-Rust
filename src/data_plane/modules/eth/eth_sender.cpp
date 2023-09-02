//
// Created by zengqihuan on 2023-8-25
//

#include <omnistack/module/module.hpp>
#include <omnistack/node.h>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::eth_sender {

    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;

    inline constexpr char kName[] = "EthSender";

    class EthSender : public Module<EthSender, kName> {
    public:
        EthSender() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    bool EthSender::DefaultFilter(Packet* packet) {
        // confirm header protocol is ipv4/ipv6
        auto proto = (*packet->node_).info_.network_layer_type;
        return (proto == NetworkLayerType::kIPv4) || (proto == NetworkLayerType::kIPv6);
    }

    Packet* EthSender::MainLogic(Packet* packet) {
        // edit omnistack's header
        packet->AddHeaderOffset(sizeof(EthernetHeader));
        auto& eth = packet->l2_header;
        eth.length_ = sizeof(EthernetHeader);
        eth.offset_ = 0;
        packet->data_ -= sizeof(EthernetHeader);
        packet->length_ += sizeof(EthernetHeader);
        auto eth_header = reinterpret_cast<EthernetHeader*>(packet->data_.Get());

        // edit eth header
        // TODO: set the MAC address according to packet->nic_
        memset(eth_header->dst, packet->nic_, sizeof(eth_header->dst));
        memset(eth_header->src, packet->nic_, sizeof(eth_header->src));
        if((*packet->node_).info_.network_layer_type == NetworkLayerType::kIPv4)
            eth_header->type = ETH_PROTO_TYPE_IPV4;
        else if((*packet->node_).info_.network_layer_type == NetworkLayerType::kIPv6)
            eth_header->type = ETH_PROTO_TYPE_IPV6;
        else
            eth_header->type = ETH_PROTO_TYPE_ARP;
        return packet;
    }

}