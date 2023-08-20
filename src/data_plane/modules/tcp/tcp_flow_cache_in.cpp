//
// Created by liuhao on 23-8-10.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/module/module.hpp>

namespace omnistack::data_plane::tcp_flow_cache_in {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpFlowCacheIn";

    constexpr uint32_t kTcpFlowCacheSize = 1 << 8;
    constexpr uint32_t kTcpFlowCacheMask = kTcpFlowCacheSize - 1;

    class TcpFlowCacheIn : public Module<TcpFlowCacheIn, kName> {
    public:
        TcpFlowCacheIn() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }

    private:
        TcpSharedHandle* tcp_shared_handle_;
        TcpFlow* flow_cache_[kTcpFlowCacheSize];
    };

    bool TcpFlowCacheIn::DefaultFilter(Packet* packet) {
        auto& ip = packet->packet_headers_[packet->header_tail_ - 2];
        Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + ip.offset_);
        if(ipv4_header->version == 4) [[likely]] return ipv4_header->proto == IP_PROTO_TYPE_TCP;
        Ipv6Header* ipv6_header = reinterpret_cast<Ipv6Header*>(packet->data_ + ip.offset_);
        if(ipv6_header->version == 6) [[likely]] return ipv6_header->nh == IP_PROTO_TYPE_TCP;
        return false;
    }

    Packet* TcpFlowCacheIn::MainLogic(Packet* packet) {
        auto flow = flow_cache_[packet->flow_hash_ & kTcpFlowCacheMask];

        auto& tcp = packet->packet_headers_[packet->header_tail_ - 1];
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + tcp.offset_);
        auto& ip = packet->packet_headers_[packet->header_tail_ - 2];
        auto ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + ip.offset_);
        uint32_t local_ip = ipv4_header->dst;
        uint32_t remote_ip = ipv4_header->src;
        uint16_t local_port = tcp_header->dport;
        uint16_t remote_port = tcp_header->sport;

        if(flow != nullptr) [[likely]] {
            bool is_same = flow->local_ip_ == local_ip && flow->remote_ip_ == remote_ip && flow->local_port_ == local_port && flow->remote_port_ == remote_port;
            if(flow->state_ == TcpFlow::State::kClosed || !is_same) [[unlikely]] {
                tcp_shared_handle_->ReleaseFlow(flow);
                flow = nullptr;
            }
        }

        if(flow == nullptr) [[unlikely]] {
            flow = tcp_shared_handle_->GetFlow(local_ip, remote_ip, local_port, remote_port);
            if(flow == nullptr) return packet;
            flow_cache_[packet->flow_hash_ & kTcpFlowCacheMask] = flow;
            tcp_shared_handle_->AcquireFlow(flow);
        }

        packet->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);
        return packet;
    }

    void TcpFlowCacheIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        for(auto& i : flow_cache_) i = nullptr;
    }

    void TcpFlowCacheIn::Destroy() {
        for(auto& i : flow_cache_) {
            if(i != nullptr) tcp_shared_handle_->ReleaseFlow(i);
        }
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
