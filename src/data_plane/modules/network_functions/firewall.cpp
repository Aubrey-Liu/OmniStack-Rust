#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>
#include <omnistack/hashtable/hashtable.h>
#include <fstream>
#include <omnistack/common/config.h>

namespace omnistack::data_plane::firewall {
    using namespace omnistack::common;
    using namespace omnistack::packet;

    struct FireWallEntry {
        uint32_t src_ipv4;
        uint32_t dst_ipv4;
        uint8_t src_cidr;
        uint8_t dst_cidr;
        uint32_t src_cidr_mask;
        uint32_t dst_cidr_mask;
        uint32_t src_port;
        uint32_t dst_port;
        uint16_t nic;
        uint16_t* l2_proto;
        uint8_t* l3_proto;
        uint32_t l2_proto_count;
        uint32_t l3_proto_count;

        void Init() {
            src_ipv4 = 0;
            dst_ipv4 = 0;
            src_cidr = 0;
            dst_cidr = 0;
            src_cidr_mask = 0;
            dst_cidr_mask = 0;
            src_port = UINT32_MAX;
            dst_port = UINT32_MAX;
            nic = UINT16_MAX;
            l2_proto = nullptr;
            l3_proto = nullptr;
            l2_proto_count = 0;
            l3_proto_count = 0;
        }

        inline bool InL2(uint16_t proto) {
            if (l2_proto_count == 0)
                return true;
            for (int i = 0; i < l2_proto_count; i ++) {
                if (l2_proto[i] == proto)
                    return true;
            }
            return false;
        }

        inline bool InL3(uint8_t proto) {
            if (l3_proto_count == 0)
                return true;
            for (int i = 0; i < l3_proto_count; i ++) {
                if (l3_proto[i] == proto)
                    return true;
            }
            return false;
        }
        
        bool IsMatch(packet::Packet* packet) {
            auto ethh = packet->GetL2Header<common::EthernetHeader>();
            if (!InL2(ethh->type))
                return false;
            
            switch (ethh->type) {
                case ETH_PROTO_TYPE_IPV4: {
                    auto iph = packet->GetL3Header<common::Ipv4Header>();
                    if (src_ipv4 != 0 && (iph->src & src_cidr_mask) != (src_ipv4 & src_cidr_mask))
                        return false;
                    if (dst_ipv4 != 0 && (iph->dst & dst_cidr_mask) != (dst_ipv4 & dst_cidr_mask))
                        return false;
                    if (!InL3(iph->proto))
                        return false;
                    if (src_port != UINT32_MAX) {
                        if (iph->proto == IP_PROTO_TYPE_TCP) {
                            auto tcph = packet->GetL4Header<common::TcpHeader>();
                            if (tcph->sport != src_port)
                                return false;
                        } else if (iph->proto == IP_PROTO_TYPE_UDP) {
                            auto udph = packet->GetL4Header<common::UdpHeader>();
                            if (udph->sport != src_port)
                                return false;
                        } else {
                            return false;
                        }
                    }
                    if (dst_port != UINT32_MAX) {
                        if (iph->proto == IP_PROTO_TYPE_TCP) {
                            auto tcph = packet->GetL4Header<common::TcpHeader>();
                            if (tcph->dport != dst_port)
                                return false;
                        } else if (iph->proto == IP_PROTO_TYPE_UDP) {
                            auto udph = packet->GetL4Header<common::UdpHeader>();
                            if (udph->dport != dst_port)
                                return false;
                        } else {
                            return false;
                        }
                    }
                    break;
                }
            }
            return false;
        }
    };

    inline constexpr char kName[] = "FireWall";

    class FireWall : public Module<FireWall, kName> {
    public:
        FireWall() {}
        virtual ~FireWall() {}

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override; 
    
    private:
        FireWallEntry* black_rules_[1024];
        int num_black_rules_;
        FireWallEntry* white_rules_[1024];
        int num_white_rules_;

        bool use_white_list_;
    };

    void FireWall::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        num_black_rules_ = 0;
        num_white_rules_ = 0;
        use_white_list_ = false;

        auto config = config::ConfigManager::GetModuleConfig("FireWall");

        auto default_strategy = config["default_strategy"];
        if (!default_strategy.isNull()) {
            if (!default_strategy.isString()) {
                OMNI_LOG(common::kFatal) << "FireWall: default_strategy is not a string" << std::endl;
                exit(1);
            }
            if (default_strategy.asString() != "DROP" && default_strategy.asString() != "PASS") {
                OMNI_LOG(common::kFatal) << "FireWall: default_strategy is not PASS or DROP" << std::endl;
                exit(1);
            }
            if (default_strategy.asString() == "PASS") {
                use_white_list_ = false;
            } else {
                use_white_list_ = true;
            }
        }

        auto rules = config["rules"];
        if (rules.isNull())
            return;
        if (!rules.isArray()) {
            OMNI_LOG(common::kFatal) << "FireWall: rules is not an array" << std::endl;
            exit(1);
        }
        for (auto rule : rules) {
            if (!rule.isObject()) {
                OMNI_LOG(common::kFatal) << "FireWall: rule is not an object" << std::endl;
                exit(1);
            }

            auto type = rule["type"];
            if (type.isNull()) {
                OMNI_LOG(common::kFatal) << "FireWall: rule does not have a type" << std::endl;
                exit(1);
            }
            if (!type.isString()) {
                OMNI_LOG(common::kFatal) << "FireWall: rule type is not a string" << std::endl;
                exit(1);
            }
            if (type.asString() != "PASS" && type.asString() != "DROP") {
                OMNI_LOG(common::kFatal) << "FireWall: rule type is not PASS or DROP" << std::endl;
                exit(1);
            }

            FireWallEntry* entry = nullptr;
            if (type.asString() == "PASS") {
                entry = white_rules_[num_white_rules_ ++];
            } else {
                entry = black_rules_[num_black_rules_ ++];
            }

            entry->Init();

            auto src_ipv4 = rule["src_ipv4"];
            if (!src_ipv4.isNull()) {
                if (!src_ipv4.isString()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule src_ipv4 is not a string" << std::endl;
                    exit(1);
                }
                if (src_ipv4.asString() == "*") {
                    entry->src_ipv4 = 0;
                    entry->src_cidr = 0;
                    entry->src_cidr_mask = 0;
                } else {
                    auto slash = src_ipv4.asString().find('/');
                    if (slash == std::string::npos) {
                        entry->src_ipv4 = inet_addr(src_ipv4.asString().c_str());
                        entry->src_cidr = 32;
                        entry->src_cidr_mask = 0xFFFFFFFF;
                    } else {
                        entry->src_ipv4 = inet_addr(src_ipv4.asString().substr(0, slash).c_str());
                        entry->src_cidr = atoi(src_ipv4.asString().substr(slash + 1).c_str());
                        entry->src_cidr_mask = 0xFFFFFFFF << (32 - entry->src_cidr);
                    }
                }
            }

            auto dst_ipv4 = rule["dst_ipv4"];
            if (!dst_ipv4.isNull()) {
                if (!dst_ipv4.isString()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule dst_ipv4 is not a string" << std::endl;
                    exit(1);
                }
                if (dst_ipv4.asString() == "*") {
                    entry->dst_ipv4 = 0;
                    entry->dst_cidr = 0;
                    entry->dst_cidr_mask = 0;
                } else {
                    auto slash = dst_ipv4.asString().find('/');
                    if (slash == std::string::npos) {
                        entry->dst_ipv4 = inet_addr(dst_ipv4.asString().c_str());
                        entry->dst_cidr = 32;
                        entry->dst_cidr_mask = 0xFFFFFFFF;
                    } else {
                        entry->dst_ipv4 = inet_addr(dst_ipv4.asString().substr(0, slash).c_str());
                        entry->dst_cidr = atoi(dst_ipv4.asString().substr(slash + 1).c_str());
                        entry->dst_cidr_mask = 0xFFFFFFFF << (32 - entry->dst_cidr);
                    }
                }
            }

            auto src_port = rule["src_port"];
            if (!src_port.isNull()) {
                if (!src_port.isString()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule src_port is not a string" << std::endl;
                    exit(1);
                }
                if (src_port.asString() == "*") {
                    entry->src_port = UINT32_MAX;
                } else {
                    entry->src_port = htons(atoi(src_port.asString().c_str()));
                }
            }

            auto dst_port = rule["dst_port"];
            if (!dst_port.isNull()) {
                if (!dst_port.isString()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule dst_port is not a string" << std::endl;
                    exit(1);
                }
                if (dst_port.asString() == "*") {
                    entry->dst_port = UINT32_MAX;
                } else {
                    entry->dst_port = htons(atoi(dst_port.asString().c_str()));
                }
            }

            auto nic = rule["nic"];
            if (!nic.isNull()) {
                if (!nic.isString()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule nic is not a string" << std::endl;
                    exit(1);
                }
                if (nic.asString() == "*") {
                    entry->nic = UINT16_MAX;
                } else {
                    entry->nic = atoi(nic.asString().c_str());
                }
            }

            auto l2_proto = rule["l2_proto"];
            if (!l2_proto.isNull()) {
                if (!l2_proto.isArray()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule l2_proto is not an array" << std::endl;
                    exit(1);
                }
                entry->l2_proto_count = l2_proto.size();
                entry->l2_proto = 
                    (uint16_t*)memory::AllocateLocal(entry->l2_proto_count * sizeof(uint16_t));
                for (int i = 0; i < entry->l2_proto_count; i ++) {
                    if (!l2_proto[i].isString()) {
                        OMNI_LOG(common::kFatal) << "FireWall: rule l2_proto is not a string" << std::endl;
                        exit(1);
                    }
                    if (l2_proto[i].asString() == "*") {
                        entry->l2_proto[i] = UINT16_MAX;
                    } else if (l2_proto[i].asString() == "IP") {
                        entry->l2_proto[i] = htons(0x0800);
                    } else if (l2_proto[i].asString() == "ARP") {
                        entry->l2_proto[i] = htons(0x0806);
                    } else if (l2_proto[i].asString() == "RARP") {
                        entry->l2_proto[i] = htons(0x8035);
                    } else if (l2_proto[i].asString() == "VLAN") {
                        entry->l2_proto[i] = htons(0x8100);
                    } else if (l2_proto[i].asString() == "IPv6") {
                        entry->l2_proto[i] = htons(0x86DD);
                    } else if (l2_proto[i].asString() == "MPLS") {
                        entry->l2_proto[i] = htons(0x8847);
                    } else if (l2_proto[i].asString() == "PPPoE") {
                        entry->l2_proto[i] = htons(0x8864);
                    } else if (l2_proto[i].asString() == "LLDP") {
                        entry->l2_proto[i] = htons(0x88CC);
                    } else if (l2_proto[i].asString() == "EAPOL") {
                        entry->l2_proto[i] = htons(0x888E);
                    } else {
                        OMNI_LOG(common::kFatal) << "FireWall: rule l2_proto is not a valid protocol" << std::endl;
                        exit(1);
                    }
                }
            }

            auto l3_proto = rule["l3_proto"];
            if (!l3_proto.isNull()) {
                if (!l3_proto.isArray()) {
                    OMNI_LOG(common::kFatal) << "FireWall: rule l3_proto is not an array" << std::endl;
                    exit(1);
                }
                entry->l3_proto_count = l3_proto.size();
                entry->l3_proto = 
                    (uint8_t*)memory::AllocateLocal(entry->l3_proto_count * sizeof(uint8_t));
                for (int i = 0; i < entry->l3_proto_count; i ++) {
                    if (!l3_proto[i].isString()) {
                        OMNI_LOG(common::kFatal) << "FireWall: rule l3_proto is not a string" << std::endl;
                        exit(1);
                    }
                    if (l3_proto[i].asString() == "TCP") {
                        entry->l3_proto[i] = IP_PROTO_TYPE_TCP;
                    } else if (l3_proto[i].asString() == "UDP") {
                        entry->l3_proto[i] = IP_PROTO_TYPE_UDP;
                    }
                }
            }
        }
        return;
    }

    packet::Packet* FireWall::MainLogic(packet::Packet* packet) {
        if (use_white_list_) {
            for (int i = 0; i < num_white_rules_; i ++) {
                if (white_rules_[i]->IsMatch(packet)) {
                    return packet;
                }
            }
            packet->Release();
            return nullptr;
        } else {
            for (int i = 0; i < num_black_rules_; i ++) {
                if (black_rules_[i]->IsMatch(packet)) {
                    packet->Release();
                    return nullptr;
                }
            }
            return packet;
        }
    }
} 