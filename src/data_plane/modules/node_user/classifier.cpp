#include <omnistack/packet/packet.hpp>
#include <omnistack/module/module.hpp>
#include <omnistack/hashtable/hashtable.h>
#include <omnistack/node.h>
#include <omnistack/channel/channel.h>
#include <omnistack/common/protocol_headers.hpp>
#include <mutex>
#include <stdexcept>
#include <atomic>

#include <omnistack/node/node_events.hpp>
#include <omnistack/node/node_common.h>
#include <omnistack/tcp_common/tcp_events.hpp>

namespace omnistack::data_plane::classifier {
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "NodeClassifier";
    constexpr uint32_t kMaxNumClassifier = 32;
    constexpr uint32_t kFlowHashEntries = 1024;
    constexpr int kCacheCounterThresh = 128;
    constexpr uint32_t kFlowHashMask = kFlowHashEntries - 1;

    channel::Channel* channel_matrix_[kMaxNumClassifier][kMaxNumClassifier];
    bool channel_flushing_[kMaxNumClassifier][kMaxNumClassifier];
    int channel_flush_stack[kMaxNumClassifier][kMaxNumClassifier];
    int channel_flush_top[kMaxNumClassifier];

    struct PacketEntry {
        node::NodeInfo info_;
        std::atomic<int> ref_cnt_;
        bool invalid_;
        node::BasicNode* node_;

        void Init(node::NodeInfo& info, node::BasicNode* node) {
            info_ = info;
            ref_cnt_ = 1;
            invalid_ = false;
            node_ = node;
        }
    };

    class NodeUserClassifier : public Module<NodeUserClassifier, kName> {
    public:
        NodeUserClassifier() {}

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;

        Packet* EventCallback(Event* event) override;

        std::vector<Event::EventType> RegisterEvents() override;
    private:
        hashtable::Hashtable* node_table_;

        static std::mutex init_lock_;
        static int num_classifier_;
        int id_;

        PacketEntry* flow_table_[kFlowHashEntries];
        int flow_table_counter_[kFlowHashEntries];
        static memory::MemoryPool* entry_pool_;
        
        void CleanUpEntry(node::NodeInfo& key);
        inline void DecRefEntry(PacketEntry* entry);
        inline bool Match(node::NodeInfo& mask, node::NodeInfo& cur) {
            if (mask.network_layer_type != cur.network_layer_type
                || mask.transport_layer_type != cur.transport_layer_type) [[unlikely]] {
                return false;
            }

            if (mask.transport.sport != cur.transport.sport) [[unlikely]] { return false; }
            if (mask.transport.dport != cur.transport.dport) [[unlikely]] { return false; }
            if (mask.network_layer_type == node::NetworkLayerType::kIPv4) [[likely]] {
                if (mask.network.ipv4.sip != cur.network.ipv4.sip) [[unlikely]] { return false; }
                if (mask.network.ipv4.dip != cur.network.ipv4.dip) [[unlikely]] { return false; }
            } else {
                if (mask.network.ipv6.sip != cur.network.ipv6.sip) [[unlikely]] { return false; }
                if (mask.network.ipv6.dip != cur.network.ipv6.dip) [[unlikely]] { return false; }
            }
            return true;
        }
    };

    void NodeUserClassifier::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        node_table_ = hashtable::Hashtable::Create("omni_node_classifier_table", 1024, sizeof(node::NodeInfo));

        {
            std::unique_lock _lock(init_lock_);
            id_ = node_common::GetCurrentGraphId(std::string(name_prefix));
            if (id_ == 0) {
                entry_pool_ = memory::AllocateMemoryPool("omni_node_classifier_entry_pool", sizeof(PacketEntry), 16384);
                if (entry_pool_ == nullptr)
                    throw std::runtime_error("Failed to allocate memory pool");
            }
        }
    }

    Packet* NodeUserClassifier::TimerLogic(uint64_t tick) {
        if (channel_flush_top[id_]) [[unlikely]] {
            while (channel_flush_top[id_] > 0) {
                auto channel_id = channel_flush_stack[id_][--channel_flush_top[id_]];
                channel_flushing_[id_][channel_id] = false;
                channel_matrix_[id_][channel_id]->Flush();
            }
        }

        int max_retries = 16;
        packet::Packet* ret = nullptr;
        packet::Packet* ret_tail = nullptr;
        for (int i = 0; i < kMaxNumClassifier && max_retries; i ++) {
            if (channel_matrix_[i][id_] == nullptr) [[unlikely]] {
                continue;
            }
            while (max_retries) {
                auto packet = reinterpret_cast<packet::Packet*>(
                    channel_matrix_[i][id_]->Read());
                if (!packet) break;

                if (ret == nullptr) {
                    ret = packet;
                    ret_tail = packet;
                } else {
                    ret_tail->next_packet_ = packet;
                    ret_tail = packet;
                }

                /// TODO: Add packet to flow director

                max_retries --;
            }
        }

        return ret;
    }

    Packet* NodeUserClassifier::MainLogic(Packet* packet) {
        node::NodeInfo tmp_info;
        auto l2_hdr = reinterpret_cast<EthernetHeader*>(packet->GetHeaderPayload(0));
        uint8_t l4_type;
        switch (l2_hdr->type) {
            case ETH_PROTO_TYPE_IPV4: {
                auto l3_hdr = reinterpret_cast<Ipv4Header*>(packet->GetHeaderPayload(1));
                l4_type = l3_hdr->proto;
                tmp_info.network_layer_type = node::NetworkLayerType::kIPv4;
                tmp_info.network.Set(l3_hdr->dst, l3_hdr->src);
                break;
            }
            case ETH_PROTO_TYPE_IPV6: {
                packet->Release();
                return nullptr;
            }
            default:
                return packet;
        }
        switch (l4_type) {
            case IP_PROTO_TYPE_TCP: {
                auto l4_hdr = reinterpret_cast<TcpHeader*>(packet->GetHeaderPayload(2));
                tmp_info.transport_layer_type = node::TransportLayerType::kTCP;
                tmp_info.transport.sport = l4_hdr->dport;
                tmp_info.transport.dport = l4_hdr->sport;
                break;
            }
            case IP_PROTO_TYPE_UDP: {
                auto l4_hdr = reinterpret_cast<UdpHeader*>(packet->GetHeaderPayload(2));
                tmp_info.transport_layer_type = node::TransportLayerType::kUDP;
                tmp_info.transport.sport = l4_hdr->dport;
                tmp_info.transport.dport = 0;
                if (l2_hdr->type == ETH_PROTO_TYPE_IPV4) [[likely]] {
                    tmp_info.network.ipv4.dip = 0;
                } else {
                    tmp_info.network.ipv6.dip = 0;
                }
                break;
            }
            default:
                return packet;
        }
        PacketEntry* entry;
        bool can_cache = true;
        if (packet->flow_hash_ != 0) [[likely]] {
            entry = flow_table_[packet->flow_hash_ & kFlowHashMask];
            if (entry) [[likely]] {
                if (!entry->invalid_) [[likely]] {
                    if (Match(entry->info_, tmp_info)) [[likely]] {
                        flow_table_counter_[packet->flow_hash_ & kFlowHashMask] = kCacheCounterThresh;
                        return packet;
                    } else {
                        flow_table_counter_[packet->flow_hash_ & kFlowHashMask] --;
                        if (!flow_table_counter_[packet->flow_hash_ & kFlowHashMask]) [[unlikely]] {
                            DecRefEntry(entry);
                            flow_table_[packet->flow_hash_ & kFlowHashMask] = nullptr;
                        } else {
                            can_cache = false;
                        }
                    }
                } else {
                    DecRefEntry(entry);
                    flow_table_[packet->flow_hash_ & kFlowHashMask] = nullptr;
                }
            }
        } else can_cache = false;
        bool is_determined = true;
        entry = reinterpret_cast<PacketEntry*>(node_table_->Lookup(&tmp_info, tmp_info.GetHash()));
        if (!entry) [[unlikely]] {
            tmp_info.transport.dport = 0;
            if (l2_hdr->type == ETH_PROTO_TYPE_IPV4) [[likely]] {
                tmp_info.network.ipv4.dip = 0;
            } else {
                tmp_info.network.ipv6.dip = 0;
            }
            entry = reinterpret_cast<PacketEntry*>(node_table_->Lookup(&tmp_info, tmp_info.GetHash()));
            if (entry == nullptr) [[unlikely]] {
                packet->Release();
                return nullptr;
            }
            is_determined = false;
        }
        auto node = entry->node_;
        int target_id = node->com_user_id_;
        if (node->num_graph_usable_) [[unlikely]] {
            target_id = node->graph_usable_[tmp_info.GetHash() % node->num_graph_usable_];
        }
        if (target_id == id_) [[likely]] {
            if (can_cache && is_determined) {
                ++entry->ref_cnt_;
                flow_table_[packet->flow_hash_ & kFlowHashMask] = entry;
                flow_table_counter_[packet->flow_hash_ & kFlowHashMask] = kCacheCounterThresh;
            }
            return packet;
        }
        if (!channel_matrix_[id_][node->com_user_id_]) [[unlikely]] {
            channel_matrix_[id_][node->com_user_id_]
                = channel::GetChannel("omni_node_user_" + std::to_string(id_) + "_to_"
                    + std::to_string(node->com_user_id_));
        }
        channel_matrix_[id_][node->com_user_id_]->Write(packet);
        if (!channel_flushing_[id_][node->com_user_id_]) {
            channel_flushing_[id_][node->com_user_id_] = true;
            channel_flush_stack[id_][channel_flush_top[id_]++] = 
                node->com_user_id_;
        }
        return nullptr;
    }

    std::vector<Event::EventType> NodeUserClassifier::RegisterEvents() {
        return std::vector<Event::EventType>{
            node_common::kNodeEventTypeUdpClosed,
            node_common::kNodeEventTypeTcpClosed,
            node_common::kNodeEventTypeAnyInsert,
            tcp_common::kTcpEventTypeClosed
        };
    }

    inline void NodeUserClassifier::DecRefEntry(PacketEntry* entry) {
        -- entry->ref_cnt_;
        if (entry->ref_cnt_ == 0)
            memory::MemoryPool::PutBack(entry);
    }

    inline void NodeUserClassifier::CleanUpEntry(node::NodeInfo& key) {
        auto entry = reinterpret_cast<PacketEntry*>(node_table_->Lookup(&key, key.GetHash()));
        if (entry != nullptr) [[likely]] {
            entry->invalid_ = true;
            DecRefEntry(entry);
        }
        node_table_->Delete(&key, key.GetHash());
    }

    Packet* NodeUserClassifier::EventCallback(Event* event) {
        if (event->type_ == node_common::kNodeEventTypeAnyInsert) {
            auto event_ = reinterpret_cast<node_common::NodeEventAnyInsert*>(event);
            auto new_entry = reinterpret_cast<PacketEntry*>(entry_pool_->Get());
            new_entry->Init(event_->node_->info_, event_->node_);
            node_table_->Insert(new_entry, new_entry, new_entry->info_.GetHash());
        } else {
            if (tcp_common::kTcpEventTypeClosed) [[likely]] {
                auto event_ = reinterpret_cast<tcp_common::TcpEventClosed*>(event);
                node::NodeInfo tmp_info;
                tmp_info.network_layer_type = node::NetworkLayerType::kIPv4;
                tmp_info.network.Set(event_->local_ipv4_, event_->remote_ipv4_);
                tmp_info.transport_layer_type = node::TransportLayerType::kTCP;
                tmp_info.transport.sport = event_->local_port_;
                tmp_info.transport.dport = event_->remote_port_;
                CleanUpEntry(tmp_info);
            } else if (event->type_ == node_common::kNodeEventTypeUdpClosed) {
                auto event_ = reinterpret_cast<node_common::NodeEventUdpClosed*>(event);
                auto& info = event_->node_->info_;
                CleanUpEntry(info);
            } else if (event->type_ == node_common::kNodeEventTypeTcpClosed) {
                // Only If passive open node
                auto event_ = reinterpret_cast<node_common::NodeEventTcpClosed*>(event);
                auto& info = event_->node_->info_;
                if (info.network_layer_type == node::NetworkLayerType::kIPv4 && info.network.ipv4.dip == 0) [[likely]] {
                    CleanUpEntry(info);
                } else if (info.network_layer_type == node::NetworkLayerType::kIPv6 && info.network.ipv6.dip == 0) [[unlikely]] {
                    CleanUpEntry(info);
                }
            }
        }
        return nullptr;
    }
}