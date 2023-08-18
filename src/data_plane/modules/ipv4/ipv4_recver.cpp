//
// Created by zengqihuan on 2023-8-19
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::ipv4_recver {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "Ipv4Recver";

    class Ipv4Recver : public Module<Ipv4Recver, kName> {
    public:
        Ipv4Recver() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_node, uint32_t global_id) override { return DefaultFilter; };

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

    private:
        FILE* ipv4_recver_log = NULL;
    };

    void Ipv4Recver::Initialize(std::string_view name_prefix, PacketPool* packet_pool)
    {
        ipv4_recver_log = fopen("./ipv4_recver_log.txt", "w");
        fprintf(ipv4_recver_log, "Initialize(): finished.\n");
        return;
    }

    void Ipv4Recver::Destroy()
    {
        fprintf(ipv4_recver_log, "Destroy(): finished.\n");
        if(ipv4_recver_log != NULL) fclose(ipv4_recver_log);
        return;
    }

    uint16_t compute_checksum(Ipv4Header* header, uint16_t length)
    {
        uint8_t* pos = (uint8_t*) header;
        uint32_t sum = 0;
        for(int i = 0;i < length;i += 2)
        {
            sum += *((uint16_t*)&pos[i]);
        }
        sum = (sum >> 16) + (sum & 0xffff);
        sum = (sum >> 16) + (sum & 0xffff);
        return (uint16_t)(~sum);
    }

    bool check_chksum(Ipv4Header* header, uint16_t length, uint16_t chksum)
    {
        header->chksum = 0;
        bool res = compute_checksum(header, length);
        header->chksum = chksum;
        return res;
    }

    inline void LogIpv4Address(const char* message, uint32_t ip) {
        printf("%s%d.%d.%d.%d\n", message, ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }

    bool Ipv4Recver::DefaultFilter(Packet* packet) {
        auto& eth = packet->packet_headers_[0];
        EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(packet->data_ + eth.offset_);
        return eth_header->type == ETH_PROTO_TYPE_IPV4;
    }

    Packet* Ipv4Recver::MainLogic(Packet* packet) {
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->offset_);
        auto& ipv4 = packet->packet_headers_[packet->header_tail_ ++];
        ipv4.length_ = ipv4_header->ihl << 2;
        ipv4.offset_ = packet->offset_;
        packet->length_ = ntohs(ipv4_header->len) + packet->offset_;
        packet->offset_ += ipv4.length_;

        if (ipv4_header->ihl < 5 || ipv4_header->ttl == 0 || check_chksum(ipv4_header, ipv4_header->ihl << 2, ipv4_header->chksum) == false)
        {
            return NULL; // drop packet
        }
        fprintf(ipv4_recver_log, "MainLogic: packet with id %d has survived from chksum check.\n", ipv4_header->id);
        // TODO: collect frags together

        // TODO: send packet to upper modules due to header->proto == TCP/UDP
        /* TODO: define a set of log helper */
#ifdef OMNI_DEBUG
        LogIpv4Address("[Ipv4Recver] src = ", ipv4_header->src);
        LogIpv4Address("[Ipv4Recver] dst = ", ipv4_header->dst);
#endif

        return packet;
    }

}
