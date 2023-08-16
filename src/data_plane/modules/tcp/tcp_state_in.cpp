//
// Created by liuhao on 23-8-11.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/random.hpp>

namespace omnistack::data_plane::tcp_state_in {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpStateIn";

    class TcpStateIn : public Module<TcpStateIn, kName> {
    public:
        TcpStateIn() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
    private:
        Packet* EnterSynReceived(TcpListenFlow* listen_flow, TcpFlow* flow, Packet* packet, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp);

        Packet* EnterEstablished(TcpFlow* flow, Packet* packet, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp, uint32_t echo_timestamp);

        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
    };

    bool TcpStateIn::DefaultFilter(Packet* packet) {
        return true;
    }

    inline Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    inline Packet* TcpStateIn::EnterSynReceived(TcpListenFlow* listen_flow, TcpFlow* flow, Packet* packet, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp) {
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + packet->packet_headers_[packet->header_tail_ - 1].offset_);

#if defined (OMNI_TCP_OPTION_MSS)
        flow->mss_ = remote_mss ? std::min(remote_mss, kTcpMaxSegmentSize) : kTcpMaxSegmentSize;
#else 
        flow->mss_ = kTcpMaxSegmentSize;
#endif

        /* set receive variables */
        auto& recv_var = flow->receive_variables_;
        recv_var.irs_ = ntohl(tcp_header->seq);
        recv_var.recv_nxt_ = recv_var.irs_ + 1;
        recv_var.recv_wnd_ = kTcpReceiveWindow;
#if defined (OMNI_TCP_OPTION_WSOPT)
        recv_var.recv_wscale_ = kTcpReceiveWindowScale;
#else
        recv_var.recv_wscale_ = 0;
#endif
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#else 
        recv_var.timestamp_recent_ = 0;
#endif

        if(flow->state_ == TcpFlow::State::kListen) {
            /* set send variables */
            auto& send_var = flow->send_variables_;
            send_var.iss_ = Rand32();
            send_var.send_una_ = send_var.iss_;
            send_var.send_nxt_ = send_var.iss_;
#if defined (OMNI_TCP_OPTION_WSOPT)
            send_var.send_wnd_ = ntohs(tcp_header->window) << remote_wscale;
            send_var.send_wscale_ = remote_wscale;
#else
            send_var.send_wnd_ = ntohs(tcp_header->window);
#endif
            send_var.send_wl1_ = send_var.iss_ - 1;
            send_var.send_wl2_ = recv_var.irs_;
            send_var.rxtcur_ = kTcpInitialRetransmissionTimeout;
            send_var.rto_begin_ = 0;
            send_var.rto_timeout_ = 0;

            /* set tcp congestion control algorithm */
            flow->congestion_control_ = TcpCongestionControlFactory::instance_().Create(listen_flow->congestion_control_algorithm_, flow);
        }

        /* reply SYN-ACK */
        auto reply = BuildReplyPacketWithFullOptions(flow, 0, packet_pool_);
        if(reply == nullptr) {
            tcp_shared_handle_->ReleaseFlow(flow);
            return TcpInvalid(packet);
        }
        reply->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);

        if(flow->state_ == TcpFlow::State::kListen) flow->send_variables_.send_nxt_ ++;
        flow->state_ = TcpFlow::State::kSynReceived;

        packet->Release();
        return reply;
    }

    inline Packet* TcpStateIn::EnterEstablished(TcpFlow* flow, Packet* packet, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp, uint32_t echo_timestamp) {
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + packet->packet_headers_[packet->header_tail_ - 1].offset_);

        if(remote_mss) flow->mss_ = std::min(remote_mss, flow->mss_);

        /* set receive variables */
        auto& recv_var = flow->receive_variables_;
        recv_var.irs_ = ntohl(tcp_header->seq);
        recv_var.recv_nxt_ = recv_var.irs_ + 1 + packet->length_ - packet->offset_;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif

        /* set send variables */
        auto& send_var = flow->send_variables_;
        send_var.send_wl2_ = recv_var.irs_;
        send_var.send_una_ = ntohl(tcp_header->ACK);
        send_var.rto_begin_ = 0;
        send_var.rto_timeout_ = 0;
#if defined (OMNI_TCP_OPTION_WSOPT)
        if(tcp_header->syn) send_var.send_wscale_ = remote_wscale;
        send_var.send_wnd_ = ntohs(tcp_header->window) << send_var.send_wscale_;
#else
        send_var.send_wnd_ = ntohs(tcp_header->window);
#endif

        /* reply ACK */
        auto reply = BuildReplyPacket(flow, 0, packet_pool_);
        if(reply == nullptr) return TcpInvalid(packet);
        reply->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);

        flow->state_ = TcpFlow::State::kEstablished;

        return reply;
    }

    Packet* TcpStateIn::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
        auto& tcp = packet->packet_headers_[packet->header_tail_ - 1];
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + tcp.offset_);
        auto ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->packet_headers_[packet->header_tail_ - 2].offset_);

        uint32_t local_ip = ipv4_header->dst;
        uint32_t remote_ip = ipv4_header->src;
        uint16_t local_port = tcp_header->dport;
        uint16_t remote_port = tcp_header->sport;

        uint8_t remote_wscale = 0;
        uint16_t remote_mss = 0;
        uint32_t remote_timestamp = 0;
        uint32_t echo_timestamp = 0;
        DecodeOptions(tcp_header, tcp.length_, &remote_mss, &remote_wscale, nullptr, nullptr, &remote_timestamp, &echo_timestamp);

        if(tcp_header->rst) [[unlikely]] {
            /* TODO: handle reset */
        }

        Packet* ret;

        if(tcp_header->syn) [[unlikely]] {
            if(!tcp_header->ack) {
                /* handle SYN */
                if(flow == nullptr) {
                    /* passively connect */
                    auto listen_flow = tcp_shared_handle_->GetListenFlow(local_ip, local_port);
                    if(listen_flow == nullptr) return TcpInvalid(packet);
                    flow = tcp_shared_handle_->CreateFlow(local_ip, remote_ip, local_port, remote_port);
                    if(flow == nullptr) return TcpInvalid(packet);
                    flow->state_ = TcpFlow::State::kListen;
                    ret = EnterSynReceived(listen_flow, flow, packet, remote_mss, remote_wscale, remote_timestamp);
                    /* TODO: raise event: new connection */
                }
                else if(flow != nullptr && flow->state_ == TcpFlow::State::kSynSent) {
                    /* simultaneously connect */
                    ret = EnterSynReceived(nullptr, flow, packet, remote_mss, remote_wscale, remote_timestamp);
                }
                else return TcpInvalid(packet);
            }
            else {
                /* handle SYN-ACK */
                if(flow == nullptr) return TcpInvalid(packet);
                if(tcp_header->ACK != htonl(flow->send_variables_.iss_ + 1)) return TcpInvalid(packet);
                if(flow->state_ == TcpFlow::State::kSynSent) {
                    /* actively connect */
                    ret = EnterEstablished(flow, packet, remote_mss, remote_wscale, remote_timestamp, echo_timestamp);
                    /* TODO: raise event: connection established */
                }
                else if(flow->state_ == TcpFlow::State::kSynReceived) {
                    /* simultaneously connect */
                    if(tcp_header->seq != htonl(flow->receive_variables_.recv_nxt_)) return TcpInvalid(packet);
                    ret = EnterEstablished(flow, packet, remote_mss, remote_wscale, remote_timestamp, echo_timestamp);
                }
                else return TcpInvalid(packet);
            }
        }
        else if(tcp_header->fin) [[unlikely]] {
            if(!tcp_header->ack) {
                /* handle FIN */
            }
            else {
                /* handle FIN-ACK */
            }
        }
        else if(tcp_header->ack) [[likely]] {
            /* handle ACK */
        }

        return ret;
    }

    void TcpStateIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpStateIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
