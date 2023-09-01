//
// Created by zengqihuan on 2023-8-15
//
// TODO: enable debug log here
#define IPV4_SENDER_DEBUG

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/node.h>
#include <deque>
#include <stdio.h>

namespace omnistack::data_plane::ipv4_sender {

    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;

    inline constexpr char kName[] = "Ipv4Sender";
    constexpr uint32_t route_table_max_size = 10;

    class Route {
    public:
        uint32_t ip_addr;
        uint8_t cidr;
        uint32_t cidr_mask;
        uint16_t nic;

        Route(uint32_t _ip_addr, uint8_t _cidr, uint16_t _nic)
        {
            ip_addr = _ip_addr;
            cidr = _cidr;
            cidr_mask = ~((1 << (32 - cidr)) - 1);
            nic = _nic;
        }
    };

    class Ipv4Sender : public Module<Ipv4Sender, kName> {
    public:

        std::deque<Route> route_table = std::deque<Route>();

        Ipv4Sender() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }

        void EditIpv4Header(Ipv4Header* header, uint8_t ihl, uint16_t len, uint8_t proto, uint32_t src, uint32_t dst);

    private:
        FILE* ipv4_sender_log = NULL;
    };

    bool Ipv4Sender::DefaultFilter(Packet* packet) {
        // confirm header protocol is TCP/UDP
        auto proto = (*packet->node_).info_.transport_layer_type;
        return (proto == TransportLayerType::kTCP) || (proto == TransportLayerType::kUDP);
    }

    void Ipv4Sender::EditIpv4Header(Ipv4Header* header, uint8_t ihl, uint16_t len, uint8_t proto, uint32_t src, uint32_t dst)
    {
        // assume all args are small-edian
        header->ihl = ihl;
        header->version = 4;
        header->len = htons(len);
        header->tos = 0;
        header->id = 0;
        header->frag_off = 0;
        header->ttl = 255; // default max ttl
        header->proto = proto;
        header->chksum = 0x149;
        header->src = src; // src and dst are already big-edian
        header->dst = dst;
    }

    Packet* Ipv4Sender::MainLogic(Packet* packet) {

        // assume no huge pack need to be fragile
        if(packet->length_ > 1480) [[unlikely]] return nullptr;
        // read src/dst ip from node
        uint32_t src_ip_addr = (packet->node_)->info_.network.ipv4.sip;
        uint32_t dst_ip_addr = (packet->node_)->info_.network.ipv4.dip;
        uint8_t max_cidr = 0;
        uint16_t dst_nic = 0;
        // edit omnistack's header.
        packet->AddHeaderOffset(sizeof(Ipv4Header));
        auto& ipv4 = packet->packet_headers_[packet->header_tail_ ++];
        ipv4.length_ = sizeof(Ipv4Header);
        ipv4.offset_ = 0;
        Ipv4Header* header = reinterpret_cast<Ipv4Header*>(packet->data_ + ipv4.offset_);
        packet->data_ = packet->data_ - header_ipv4.length_;
        packet->length_ += sizeof(Ipv4Header);

        // find dst ip from route table
        for(int i = 0;i < route_table.size();i++)
        {
            if(((dst_ip_addr ^ route_table[i].ip_addr) & route_table[i].cidr_mask) != 0)
            {
                if (i != route_table.size() - 1) continue;
                else
                {
                    // drop the packet if can't find a route
                    fprintf(ipv4_sender_log, "MainLogic: can't find a route from %d to %d.\n", src_ip_addr, dst_ip_addr);
                    return nullptr;
                }
            }
            if(max_cidr < route_table[i].cidr)
            {
                max_cidr = route_table[i].cidr;
                dst_nic = route_table[i].nic;
            }
        }

        packet->nic_ = dst_nic;
        // give it correct proto type
        fprintf(ipv4_sender_log, "MainLogic: found route path from %d to %d\n", src_ip_addr, dst_ip_addr);
        EditIpv4Header(header, ipv4.length_ >> 2, packet->length_, 
            ((*packet->node_).info_.transport_layer_type == TransportLayerType::kTCP) ? IP_PROTO_TYPE_TCP : IP_PROTO_TYPE_UDP, 
            src_ip_addr, dst_ip_addr);
        fprintf(ipv4_sender_log, "MainLogic: finished editing ipv4 header with check_sum 0x%x\n", header->chksum);

        return packet;
    }

    void Ipv4Sender::Initialize(std::string_view name_prefix, PacketPool* packet_pool)
    {
        ipv4_sender_log = fopen("./ipv4_sender_log.txt", "w");
        // TODO: init a correct route table
        for(int i = 0;i < route_table_max_size;i++)
        {
            route_table.push_back(Route(i, i, i));
        }
        fprintf(ipv4_sender_log, "Initialize: finished.\n");
        return;
    }

    void Ipv4Sender::Destroy()
    {
        fprintf(ipv4_sender_log, "Destroy: finished.\n");
        if(ipv4_sender_log != NULL) fclose(ipv4_sender_log);
        return;
    }

}
