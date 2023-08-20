//
// Created by liuhao on 23-8-18.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/node.h>

namespace omnistack::data_plane::tcp_flow_cache_out {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpFlowCacheOut";

    constexpr uint32_t kTcpFlowCacheSize = 1 << 8;
    constexpr uint32_t kTcpFlowCacheMask = kTcpFlowCacheSize - 1;

    class TcpFlowCacheOut : public Module<TcpFlowCacheOut, kName> {
    public:
        TcpFlowCacheOut() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
    private:
        TcpSharedHandle* tcp_shared_handle_;
        TcpFlow* flow_cache_[kTcpFlowCacheSize];
    };

    bool TcpFlowCacheOut::DefaultFilter(Packet* packet) {
        auto& node_info = packet->node_.Get()->info_;
        return node_info.transport_layer_type == node::TransportLayerType::kTCP;
    }

    inline Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    Packet* TcpFlowCacheOut::MainLogic(Packet* packet) {
        auto& node_info = packet->node_.Get()->info_;
        uint32_t local_ip = node_info.network.ipv4.sip;
        uint32_t remote_ip = node_info.network.ipv4.dip;
        uint16_t local_port = node_info.transport.tcp.sport; 
        uint16_t remote_port = node_info.transport.tcp.dport;

        TcpFlow* flow = nullptr;

        if(packet->flow_hash_) [[likely]] {
            flow = flow_cache_[packet->flow_hash_ & kTcpFlowCacheMask];
            if(flow != nullptr) [[likely]] {
                bool is_same = flow->local_ip_ == local_ip && flow->remote_ip_ == remote_ip && flow->local_port_ == local_port && flow->remote_port_ == remote_port;
                if(flow->state_ == TcpFlow::State::kClosed || !is_same) [[unlikely]] {
                    tcp_shared_handle_->ReleaseFlow(flow);
                    flow = nullptr;
                }
            }
        }

        if(flow == nullptr) [[unlikely]] {
            flow = tcp_shared_handle_->GetFlow(local_ip, remote_ip, local_port, remote_port);
            if(flow == nullptr || flow->state_ == TcpFlow::State::kClosed) [[unlikely]] return TcpInvalid(packet);
            if(packet->flow_hash_) [[likely]] {
                flow_cache_[packet->flow_hash_ & kTcpFlowCacheMask] = flow;
                tcp_shared_handle_->AcquireFlow(flow);
            }
        }

        packet->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);

        return packet;
    }

    void TcpFlowCacheOut::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        for(auto& i : flow_cache_) i = nullptr;
    }

    void TcpFlowCacheOut::Destroy() {
        for(auto& i : flow_cache_) {
            if(i != nullptr) tcp_shared_handle_->ReleaseFlow(i);
        }
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
