#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>
#include <omnistack/node.h>
#include <omnistack/hashtable/hashtable.h>
#include <omnistack/common/config.h>
#include <sys/time.h>

namespace omnistack::data_plane::nat {
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "Nat";

    struct NatEntry {
        uint16_t new_port;
        uint16_t ori_port;
        uint32_t ipv4;
    };

    class Nat : public Module<Nat, kName> {
    public:
        Nat() {}
        virtual ~Nat() {}

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;
    
    private:
        hashtable::Hashtable* table_;
        memory::MemoryPool* entry_pool_;

        uint64_t packet_count = 0;
        uint64_t packet_size_sum = 0;
        uint64_t last_print_us = 0;
    };

    void Nat::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        table_ = hashtable::Hashtable::Create(1024, sizeof(uint16_t));
        entry_pool_ = memory::AllocateMemoryPool("omni_nat_entries", sizeof(NatEntry), 1024);

        auto conf = config::ConfigManager::GetModuleConfig("Nat");
        auto entries = conf["entries"];
        for(auto& entry : entries) {
            auto ipv4_str = entry["ipv4"].asString();
            auto ipv4 = inet_addr(ipv4_str.c_str());
            auto ori_port = entry["ori_port"].asUInt();
            auto new_port = entry["new_port"].asUInt();
            auto nat_entry = (NatEntry*)entry_pool_->Get();
            nat_entry->ipv4 = ipv4;
            nat_entry->ori_port = ori_port;
            nat_entry->new_port = new_port;
            table_->Insert(nat_entry, nat_entry);
        }

        packet_count = 0;
        packet_size_sum = 0;
        last_print_us = 0;

        return;
    }

    Packet* Nat::MainLogic(Packet* packet) {
        packet_count ++;
        packet_size_sum += packet->GetLength() - sizeof(EthernetHeader)
            - sizeof(Ipv4Header) - sizeof(UdpHeader);
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        auto current_us = tv.tv_sec * 1000000 + tv.tv_usec;

        if (current_us - last_print_us > 1000000) {
            OMNI_LOG(common::kInfo) << "OmniStackIDS: " << packet_count << " packets, " << packet_size_sum << " bytes, " << packet_size_sum / packet_count << " bytes/packet\n";
            OMNI_LOG(common::kInfo) << "OmniStackIDS: " << packet_size_sum * 8.0 / (current_us - last_print_us) << " Mbps\n";
            // click_chatter("OmniStackIDS: %lu packets, %lu bytes, %lu bytes/packet", packet_count, packet_size_sum, packet_size_sum / packet_count);
            // click_chatter("OmniStackIDS: %.3f Mbps", packet_size_sum * 8.0 / (current_us - last_print_us));
            
            packet_count = 0;
            packet_size_sum = 0;
            last_print_us = current_us;
        }

        auto ethh = (EthernetHeader*)packet->GetPayload();
        if (ethh->type != ETH_PROTO_TYPE_IPV4) return packet;
        auto iph = (Ipv4Header*)(ethh + 1);
        if (iph->proto != IP_PROTO_TYPE_TCP && 
            iph->proto != IP_PROTO_TYPE_UDP) return packet;
        if (iph->proto == IP_PROTO_TYPE_TCP) {
            auto tcph = (TcpHeader*)(iph + 1);
            auto nat_entry = (NatEntry*)table_->Lookup(&tcph->dport);
            if (nat_entry != nullptr) {
                tcph->dport = nat_entry->ori_port;
                iph->dst = nat_entry->ipv4;
            }
            return packet;
        } else if (iph->proto == IP_PROTO_TYPE_UDP) {
            auto udph = (UdpHeader*)(iph + 1);
            auto nat_entry = (NatEntry*)table_->Lookup(&udph->dport);
            if (nat_entry != nullptr) {
                udph->dport = nat_entry->ori_port;
                iph->dst = nat_entry->ipv4;
            }
            return packet;
        }
        return packet;
    }
}