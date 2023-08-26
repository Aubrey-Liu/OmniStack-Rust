#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <mutex>
#include <omnistack/channel/channel.h>
#include <omnistack/hashtable/hashtable.h>
#include <omnistack/node.h>
#include <stdexcept>
#include <omnistack/tcp_common/tcp_events.hpp>
#include <omnistack/node/node_events.hpp>
#include <omnistack/memory/memory.h>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/node/node_common.h>

namespace omnistack::data_plane::node_user {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "NodeUser";

    static std::mutex init_lock;
    static int node_id = 0;

    class NodeUser : public Module<NodeUser, kName> {
    public:
        NodeUser() {}

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;

        Packet* EventCallback(Event* event) override;

        std::vector<Event::EventType> RegisterEvents() override;
    private:
        inline Packet* OnConnect(Event* event);

        inline Packet* OnDisconnect(Event* event);

        int id_;

        channel::MultiWriterChannel* node_channel_;
        hashtable::Hashtable* node_table_;
        memory::MemoryPool* event_pool_;
        packet::PacketPool* packet_pool_;
    };

    void NodeUser::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        id_ = node_common::GetCurrentGraphId(std::string(name_prefix));
        node_table_ = hashtable::Hashtable::Create("omni_node_table", 1024, sizeof(node::NodeInfo));
        node_channel_ = channel::GetMultiWriterChannel("omni_node_user_channel_" + std::to_string(id_));
        event_pool_ = memory::AllocateMemoryPool("omni_data_plane_node_user_event_pool_" + std::to_string(id_), kEventMaxLength, 512);
        packet_pool_ = packet_pool;
    }

    Packet* NodeUser::MainLogic(Packet* packet) {
        if (packet->node_.Get() != nullptr) [[likely]] {
            packet->node_->Write(packet);
        } else {
            // Search Hashtable
            node::NodeInfo tmp_node_info;
            auto l2_hdr = reinterpret_cast<EthernetHeader*>(packet->data_ + packet->packet_headers_[0].offset_);
            uint8_t l4_type = 0;
            switch (l2_hdr->type) {
                [[likely]] case ETH_PROTO_TYPE_IPV4: {
                    auto l3_hdr = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->packet_headers_[1].offset_);
                    l4_type = l3_hdr->proto;
                    tmp_node_info.network_layer_type = node::NetworkLayerType::kIPv4;
                    tmp_node_info.network.ipv4.sip = l3_hdr->dst;
                    tmp_node_info.network.ipv4.dip = l3_hdr->src;
                    tmp_node_info.network.ipv4.padding[0] = 0;
                    tmp_node_info.network.ipv4.padding[1] = 0;
                    tmp_node_info.network.ipv6.dip = 0;
                    break;
                }
                case ETH_PROTO_TYPE_IPV6: {
                    throw std::runtime_error("Not implemented IPv6 support");
                    break;
                }
                default:
                    throw std::runtime_error("Unknown L3 Type");
            }

            switch (l4_type) {
                case IP_PROTO_TYPE_TCP: {
                    auto l4_hdr = reinterpret_cast<TcpHeader*>(packet->data_ + packet->packet_headers_[2].offset_);
                    tmp_node_info.transport_layer_type = node::TransportLayerType::kTCP;
                    tmp_node_info.transport.tcp.sport = l4_hdr->dport;
                    tmp_node_info.transport.tcp.dport = l4_hdr->sport;
                    break;
                }
                case IP_PROTO_TYPE_UDP: {
                    auto l4_hdr = reinterpret_cast<UdpHeader*>(packet->data_ + packet->packet_headers_[2].offset_);
                    tmp_node_info.transport_layer_type = node::TransportLayerType::kUDP;
                    tmp_node_info.transport.udp.sport = l4_hdr->dport;
                    tmp_node_info.transport.udp.dport = l4_hdr->sport;
                    break;
                }
            }

            node::BasicNode* node = reinterpret_cast<node::BasicNode*>(node_table_->Lookup(&tmp_node_info, tmp_node_info.GetHash()));
            if (node != nullptr) [[likely]] {
                node->Write(packet);
                /// TODO: Flush Node
            } else {
                packet_pool_->Free(packet);
            }
        }
        return nullptr;
    }

    Packet* NodeUser::TimerLogic(uint64_t tick) {
        Packet* ret = nullptr;
        Packet* tail = nullptr;
        for (int i = 0; i < 32; i ++) {
            auto packet = reinterpret_cast<Packet*>(node_channel_->Read());
            if (packet == nullptr) break;

            auto header = reinterpret_cast<node::NodeCommandHeader*>(packet->data_ + packet->offset_);
            switch (header->type)
            {
            [[likely]] case node::NodeCommandType::kPacket:
                packet->offset_ += sizeof(node::NodeCommandHeader);
                if (!ret) [[unlikely]] {
                    ret = packet;
                    tail = packet;
                } else {
                    tail->next_packet_.Set(packet);
                    tail = packet;
                }
                break;
            case node::NodeCommandType::kUpdateNodeInfo: {
                using namespace omnistack::data_plane::node_common;
                auto& info = packet->node_->info_;
                info.padding = 0;
                auto hash_val = info.GetHash();
                if (node_table_->Insert(&info, packet->node_.Get(), hash_val))
                    throw std::runtime_error("Failed to insert node info into hashtable");
                packet->node_->in_hashtable_ = true;
                raise_event_(new(event_pool_->Get()) NodeEventAnyInsert(packet->node_.Get()));
                if (info.network_layer_type == node::NetworkLayerType::kIPv4) [[likely]] {
                    if (info.transport_layer_type == node::TransportLayerType::kTCP && info.transport.tcp.dport != 0) [[likely]]
                        raise_event_(new(event_pool_->Get()) NodeEventTcpConnect(packet->node_.Get()));
                }
                packet->Release();
                break;
            }
            case node::NodeCommandType::kClearNodeInfo: {
                using namespace omnistack::data_plane::node_common;
                auto& info = packet->node_->info_;
                info.padding = 0;
                auto hash_val = info.GetHash();
                if (node_table_->Delete(&info, hash_val))
                    throw std::runtime_error("Failed to delete node info from hashtable");
                packet->node_->in_hashtable_ = false;
                switch (info.network_layer_type)
                {
                    case node::NetworkLayerType::kIPv4: {
                        if (info.transport_layer_type == node::TransportLayerType::kTCP) [[likely]]
                            raise_event_(new(event_pool_->Get()) NodeEventTcpClosed(packet->node_.Get()));
                        else if (info.transport_layer_type == node::TransportLayerType::kUDP)
                            raise_event_(new(event_pool_->Get()) NodeEventUdpClosed(packet->node_.Get()));
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown network layer type");
                }
                node::ReleaseBasicNode(packet->node_.Get());
                packet->Release();
                break;
            }
            default:
                break;
            }
        }
        return ret;
    }

    std::vector<Event::EventType> NodeUser::RegisterEvents() {
        return std::vector<Event::EventType>{
            tcp_common::kTcpEventTypeConnect,
            tcp_common::kTcpEventTypeDisconnect
        };
    }

    Packet* NodeUser::EventCallback(Event* event) {
        switch (event->type_) {
            case tcp_common::kTcpEventTypeConnect:
                return OnConnect(event);
            case tcp_common::kTcpEventTypeDisconnect:
                return OnDisconnect(event);
            default:
                return nullptr;
        }
    }

    inline Packet* NodeUser::OnConnect(Event* event) {
        auto evt = reinterpret_cast<tcp_common::TcpEventConnect*>(event);
        node::NodeInfo tmp_node_info;
        tmp_node_info.network_layer_type = node::NetworkLayerType::kIPv4;
        tmp_node_info.network.Set(evt->local_ipv4_, evt->remote_ipv4_);
        tmp_node_info.transport_layer_type = node::TransportLayerType::kTCP;
        tmp_node_info.transport.tcp.sport = evt->local_port_;
        tmp_node_info.transport.tcp.dport = evt->remote_port_;

        auto active_node = reinterpret_cast<node::BasicNode*>(node_table_->Lookup(&tmp_node_info, tmp_node_info.GetHash()));
        if (active_node != nullptr) [[unlikely]] {
            // Active Open
            active_node->peer_closed_ = false;
            raise_event_(new(event_pool_->Get()) node_common::NodeEventTcpNewNode(active_node));
            return nullptr;
        }

        auto new_node = node::CreateBasicNode(id_);
        new_node->UpdateInfo(tmp_node_info);
        new_node->peer_closed_ = false;
        new_node->in_hashtable_ = true;
        if (node_table_->Insert(&tmp_node_info, new_node, tmp_node_info.GetHash()))
            throw std::runtime_error("Failed to insert node info into hashtable");
        raise_event_(new(event_pool_->Get()) node_common::NodeEventAnyInsert(new_node));
        tmp_node_info.network.Set(static_cast<uint32_t>(0), evt->local_ipv4_);
        tmp_node_info.transport.tcp.dport = 0;
        auto passive_node = reinterpret_cast<node::BasicNode*>(node_table_->Lookup(&tmp_node_info, tmp_node_info.GetHash()));
        if (!passive_node) {
            tmp_node_info.network.Set(static_cast<uint32_t>(0), static_cast<uint32_t>(0));
            passive_node = reinterpret_cast<node::BasicNode*>(node_table_->Lookup(&tmp_node_info, tmp_node_info.GetHash()));
        }
        if (!passive_node) [[unlikely]] throw std::runtime_error("Failed to find passive node");
        passive_node->Write((Packet*)new_node);
        raise_event_(new(event_pool_->Get()) node_common::NodeEventTcpNewNode(new_node));
        return nullptr;
    }

    inline Packet* NodeUser::OnDisconnect(Event* event) {
        auto evt = reinterpret_cast<tcp_common::TcpEventDisconnect*>(event);
        node::NodeInfo tmp_node_info;
        tmp_node_info.network_layer_type = node::NetworkLayerType::kIPv4;
        tmp_node_info.network.Set(evt->local_ipv4_, evt->remote_ipv4_);
        tmp_node_info.transport_layer_type = node::TransportLayerType::kTCP;
        tmp_node_info.transport.tcp.sport = evt->local_port_;
        tmp_node_info.transport.tcp.dport = evt->remote_port_;
        auto node = reinterpret_cast<node::BasicNode*>(node_table_->Lookup(&tmp_node_info, tmp_node_info.GetHash()));
        if (node != nullptr) [[likely]] {
            node->peer_closed_ = true;
            auto empty_packet = packet_pool_->Allocate();
            node->Write(empty_packet);
            node->Flush();
        }
        return nullptr;
    }
}