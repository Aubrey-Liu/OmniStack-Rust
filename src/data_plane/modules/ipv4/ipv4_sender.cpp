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
    inline constexpr uint32_t route_table_max_size = 10;

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

        Filter GetFilter(std::string_view upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }

    private:
        FILE* ipv4_sender_log = NULL;
    };

    bool Ipv4Sender::DefaultFilter(Packet* packet) {
        // TODO: confirm header protocol as TCP/UDP/ICMP
        return true;
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
        //fclose(ipv4_sender_log); will trigger the seg fault
        return;
    }

}
