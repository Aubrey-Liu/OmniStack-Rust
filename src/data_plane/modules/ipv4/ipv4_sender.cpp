//
// Created by zengqihuan on 2023-8-15
//
// enable debug log here
#define IPV4_SENDER_DEBUG

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <deque>
#include <stdio.h>

namespace omnistack::data_plane::ipv4_sender {

    using namespace omnistack::common;
    using namespace omnistack::packet;

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
        // TODO: confirm header protocol is TCP/UDP, but not nesassary currently
        return true;
    }

    void Ipv4Sender::EditIpv4Header(Ipv4Header* header, uint8_t ihl, uint16_t len, uint8_t proto, uint32_t src, uint32_t dst)
    {
        // assume all args are small edian
        header->ihl = ihl;
        header->version = 4;
        header->len = htons(len);
        header->tos = 0;
        header->id = 0;
        header->frag_off = 0;
        header->ttl = 10; // default ttl
        header->proto = proto;
        header->chksum = 0x149;
        header->src = htonl(src);
        header->dst = htonl(src);
    }

    Packet* Ipv4Sender::MainLogic(Packet* packet) {
        // TODO: find target ip according to specific upper protocol TCP/UDP/ICMP
        // using nic id instead before finishing that.
        uint32_t dst_nic = packet->nic_;

        for(int i = 0;i < route_table.size();i++)
        {
            if(dst_nic == route_table[i].nic)
            {
                // edit the packet header.
                fprintf(ipv4_sender_log, "MainLogic: found target nic %d with ip %d.\n", dst_nic, route_table[i].ip_addr);
                auto& ipv4 = packet->packet_headers_[packet->header_tail_ ++];
                ipv4.length_ = sizeof(Ipv4Header);
                ipv4.offset_ = packet->offset_;
                Ipv4Header* header = (Ipv4Header*)malloc(sizeof(Ipv4Header));
                EditIpv4Header(header, ipv4.length_ >> 2, htons(packet->length_ - packet->offset_), 0, -1, route_table[i].ip_addr);
                fprintf(ipv4_sender_log, "MainLogic: finished editing ipv4 header with check_sum 0x%x\n", header->chksum);
                free(header);
            }
        }

        return packet;
    }

    void Ipv4Sender::Initialize(std::string_view name_prefix, PacketPool* packet_pool)
    {
        ipv4_sender_log = fopen("./ipv4_sender_log.txt", "w");
        for(int i = 0;i < route_table_max_size;i++)
        {
            route_table.push_back(Route(i, i, i));
        }
        fprintf(ipv4_sender_log, "Initialize(): finished.\n");
        return;
    }

    void Ipv4Sender::Destroy()
    {
        fprintf(ipv4_sender_log, "Destroy(): finished.\n");
        if(ipv4_sender_log != NULL) fclose(ipv4_sender_log);
        return;
    }

}
