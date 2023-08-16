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
        void EnterSynReceived(TcpListenFlow* listen_flow, TcpFlow* flow, TcpHeader* tcp_header, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp);

        void EnterEstablished(TcpFlow* flow, TcpHeader* tcp_header, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp, uint32_t echo_timestamp);

        void EnterCloseWait(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp);

        void EnterClosing(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp);

        void EnterFinWait2(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp);

        void EnterTimeWait(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp);

        void EnterClosed(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp);

        void OnAck(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp, uint32_t echo_timestamp);

        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
    };

    bool TcpStateIn::DefaultFilter(Packet* packet) {
        return true;
    }

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    inline Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    inline void InitializeRtt(TcpFlow* flow, uint32_t rtt) {
        auto& send_var = flow->send_variables_;
        send_var.srtt_ = rtt;
        send_var.rttvar_ = rtt >> 1;
        send_var.rxtcur_ = send_var.srtt_ + std::max((int64_t)1, (int64_t)(4 * send_var.rttvar_));
        if(send_var.rxtcur_ < kTcpMinimumRetransmissionTimeout) send_var.rxtcur_ = kTcpMinimumRetransmissionTimeout;
    }

    inline void EstimateRtt(TcpFlow* flow, uint32_t rtt) {
        int64_t delta = rtt - flow->send_variables_.srtt_;
        uint64_t abs_delta = delta > 0 ? delta : -delta;
        auto& send_var = flow->send_variables_;
        send_var.srtt_ += delta >> 3;
        send_var.rttvar_ = (send_var.rttvar_ * 3 + abs_delta) >> 2;
        send_var.rxtcur_ = send_var.srtt_ + std::max((int64_t)1, (int64_t)(4 * send_var.rttvar_));
        if(send_var.rxtcur_ < kTcpMinimumRetransmissionTimeout) send_var.rxtcur_ = kTcpMinimumRetransmissionTimeout;
    }

    inline void TcpStateIn::EnterSynReceived(TcpListenFlow* listen_flow, TcpFlow* flow, TcpHeader* tcp_header, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp) {
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
            send_var.rxtcur_ = kTcpMinimumRetransmissionTimeout;
            send_var.rto_begin_ = 0;
            send_var.rto_timeout_ = 0;

            /* set tcp congestion control algorithm */
            flow->congestion_control_ = TcpCongestionControlFactory::instance_().Create(listen_flow->congestion_control_algorithm_, flow);
        }

        flow->state_ = TcpFlow::State::kSynReceived;
    }

    inline void TcpStateIn::EnterEstablished(TcpFlow* flow, TcpHeader* tcp_header, uint16_t remote_mss, uint8_t remote_wscale, uint32_t remote_timestamp, uint32_t echo_timestamp) {
        if(remote_mss) flow->mss_ = std::min(remote_mss, flow->mss_);

        /* set receive variables */
        auto& recv_var = flow->receive_variables_;
        if(tcp_header->syn) recv_var.irs_ = ntohl(tcp_header->seq);
        recv_var.recv_nxt_ = recv_var.irs_ + 1;
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

        /* estimate rtt */
#if defined (OMNI_TCP_OPTION_TSPOT)
        if(echo_timestamp) [[likely]] InitializeRtt(flow, (uint32_t)(NowMs() - echo_timestamp));
        else InitializeRtt(flow, (uint32_t)(NowUs() - flow->send_variables_.rto_begin_));
#else
        InitializeRtt(flow, (uint32_t)(NowUs() - flow->send_variables_.rto_begin_));
#endif

        flow->state_ = TcpFlow::State::kEstablished;
    }

    inline void TcpStateIn::EnterCloseWait(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp) {
        auto& recv_var = flow->receive_variables_;
        recv_var.recv_nxt_ ++;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif

        flow->state_ = TcpFlow::State::kCloseWait;
    }

    inline void TcpStateIn::EnterClosing(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp) {
        auto& recv_var = flow->receive_variables_;
        recv_var.recv_nxt_ ++;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif

        flow->state_ = TcpFlow::State::kClosing;
    }

    inline void TcpStateIn::EnterFinWait2(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp) {
        auto& recv_var = flow->receive_variables_;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif
            
        flow->state_ = TcpFlow::State::kFinWait2;
    }

    inline void TcpStateIn::EnterTimeWait(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp) {
        auto& recv_var = flow->receive_variables_;
        recv_var.recv_nxt_ ++;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif
    
        flow->state_ = TcpFlow::State::kTimeWait;

        /* TODO: raise event: flow timewait */
    }

    inline void TcpStateIn::EnterClosed(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp) {
        flow->state_ = TcpFlow::State::kClosed;
    }

    inline void TcpStateIn::OnAck(TcpFlow* flow, TcpHeader* tcp_header, uint32_t remote_timestamp, uint32_t echo_timestamp) {
        uint32_t ack_num = ntohl(tcp_header->ACK);
        
        /* update send variables */
        auto& send_var = flow->send_variables_;
#if defined (OMNI_TCP_OPTION_WSOPT)
        send_var.send_wnd_ = ntohs(tcp_header->window) << send_var.send_wscale_;
#else
        send_var.send_wnd_ = ntohs(tcp_header->window);
#endif

        auto& recv_var = flow->receive_variables_;
#if defined (OMNI_TCP_OPTION_TSPOT)
        recv_var.timestamp_recent_ = remote_timestamp;
#endif
        if(TcpGreaterUint32(ack_num, send_var.send_una_)) {
            /* normal ack */
            uint32_t ack_bytes = ack_num - send_var.send_una_;
            send_var.send_una_ = ack_num;
            send_var.is_retransmission_ = false;
            /* TODO: release packets in retransmission queue */

            /* estimate rtt */
#if defined (OMNI_TCP_OPTION_TSPOT)
            if(echo_timestamp) [[likely]] EstimateRtt(flow, (uint32_t)(NowMs() - echo_timestamp));
            else EstimateRtt(flow, (uint32_t)(NowUs() - flow->send_variables_.rto_begin_));
#else
            EstimateRtt(flow, (uint32_t)(NowUs() - flow->send_variables_.rto_begin_));
#endif
        }
        else if(send_var.send_nxt_ == send_var.send_una_ && ack_num == send_var.send_una_) {
            /* duplicate ack */
        }

        if(send_var.send_una_ == send_var.send_nxt_) {
            /* all packets acked */
            send_var.rto_begin_ = 0;
            send_var.rto_timeout_ = 0;
        }
        else {
            /* restart retransmission timer */
            send_var.rto_begin_ = NowUs();
            send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
        }

        /* TODO: update congestion control */
    }

    Packet* TcpStateIn::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
        if(flow != nullptr) tcp_shared_handle_->ReleaseFlow(flow);
        auto& tcp = packet->packet_headers_[packet->header_tail_ - 1];
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + tcp.offset_);
        auto ipv4_header = reinterpret_cast<Ipv4Header*>(packet->data_ + packet->packet_headers_[packet->header_tail_ - 2].offset_);

        uint32_t local_ip = ipv4_header->dst;
        uint32_t remote_ip = ipv4_header->src;
        uint16_t local_port = tcp_header->dport;
        uint16_t remote_port = tcp_header->sport;
        uint32_t ack_num = ntohl(tcp_header->ACK);
        uint32_t seq_num = ntohl(tcp_header->seq);

        uint8_t remote_wscale = 0;
        uint16_t remote_mss = 0;
        uint32_t remote_timestamp = 0;
        uint32_t echo_timestamp = 0;
        DecodeOptions(tcp_header, tcp.length_, &remote_mss, &remote_wscale, nullptr, nullptr, &remote_timestamp, &echo_timestamp);

        if(tcp_header->rst) [[unlikely]] {
            /* TODO: handle reset */
        }

        Packet* ret = nullptr;

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
                    EnterSynReceived(listen_flow, flow, tcp_header, remote_mss, remote_wscale, remote_timestamp);
                    /* reply SYN-ACK */
                    ret = BuildReplyPacketWithFullOptions(flow, 0, packet_pool_);
                    flow->send_variables_.send_nxt_ ++;
                    /* TODO: raise event: new connection */
                }
                else if(flow != nullptr && flow->state_ == TcpFlow::State::kSynSent) {
                    /* simultaneously connect */
                    EnterSynReceived(nullptr, flow, tcp_header, remote_mss, remote_wscale, remote_timestamp);
                    /* reply SYN-ACK */
                    ret = BuildReplyPacketWithFullOptions(flow, 0, packet_pool_);
                }
                else return TcpInvalid(packet);
            }
            else {
                /* handle SYN-ACK */
                if(flow == nullptr) return TcpInvalid(packet);
                if(ack_num != flow->send_variables_.iss_ + 1) return TcpInvalid(packet);
                if(flow->state_ == TcpFlow::State::kSynSent) {
                    /* actively connect */
                    EnterEstablished(flow, tcp_header, remote_mss, remote_wscale, remote_timestamp, echo_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                    /* TODO: raise event: connection established */
                }
                else if(flow->state_ == TcpFlow::State::kSynReceived) {
                    /* simultaneously connect */
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    EnterEstablished(flow, tcp_header, remote_mss, remote_wscale, remote_timestamp, echo_timestamp);
                    /* no reply */
                    /* TODO: raise event: connection established */
                }
                else return TcpInvalid(packet);
            }
        }
        else if(tcp_header->fin) [[unlikely]] {
            if(!tcp_header->ack) {
                /* handle FIN */
                if(flow == nullptr) return TcpInvalid(packet);
                if(flow->state_ == TcpFlow::State::kEstablished) {
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    /* passive close */
                    EnterCloseWait(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                    /* TODO: raise event: close connection */
                }
                else if(flow->state_ == TcpFlow::State::kFinWait1) {
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    /* simultaneous close */
                    EnterClosing(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else if(flow->state_ == TcpFlow::State::kFinWait2) {
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    EnterTimeWait(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else if(flow->state_ == TcpFlow::State::kTimeWait) {
                    if(seq_num != flow->receive_variables_.recv_nxt_ - 1) return TcpInvalid(packet);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else return TcpInvalid(packet);
            }
            else {
                /* handle FIN-ACK */
                if(flow == nullptr) return TcpInvalid(packet);
                if(ack_num != flow->send_variables_.send_nxt_) return TcpInvalid(packet);
                if(flow->state_ == TcpFlow::State::kEstablished) {
                    /* passive close */
                    EnterCloseWait(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                    /* TODO: raise event: close connection */
                }
                else if(flow->state_ == TcpFlow::State::kFinWait1) {
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    EnterTimeWait(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else if(flow->state_ == TcpFlow::State::kFinWait2) {
                    if(seq_num != flow->receive_variables_.recv_nxt_) return TcpInvalid(packet);
                    EnterTimeWait(flow, tcp_header, remote_timestamp);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else if(flow->state_ == TcpFlow::State::kClosing) {
                    if(seq_num != flow->receive_variables_.recv_nxt_ - 1) return TcpInvalid(packet);
                    EnterTimeWait(flow, tcp_header, remote_timestamp);
                    /* no reply */
                }
                else if(flow->state_ == TcpFlow::State::kTimeWait) {
                    if(seq_num != flow->receive_variables_.recv_nxt_ - 1) return TcpInvalid(packet);
                    /* reply ACK */
                    ret = BuildReplyPacket(flow, 0, packet_pool_);
                }
                else return TcpInvalid(packet);
            }
        }
        else if(tcp_header->ack) [[likely]] {
            /* handle ACK */
            if(flow == nullptr) [[unlikely]] return TcpInvalid(packet);
            if(seq_num != flow->receive_variables_.recv_nxt_) [[unlikely]] return TcpInvalid(packet);
            if(flow->state_ == TcpFlow::State::kEstablished) [[likely]] {
                if(TcpGreaterUint32(ack_num, flow->send_variables_.send_nxt_)) [[unlikely]] return TcpInvalid(packet);
                OnAck(flow, tcp_header, remote_timestamp, echo_timestamp);
            }
            else {
                /* handle ACK in other states */
                if(ack_num != flow->send_variables_.send_nxt_) return TcpInvalid(packet);
                switch (flow->state_) {
                    case TcpFlow::State::kSynReceived:
                        EnterEstablished(flow, tcp_header, remote_mss, remote_wscale, remote_timestamp, echo_timestamp);
                        /* no reply */
                        /* TODO: raise event: connection established */
                        break;
                    case TcpFlow::State::kFinWait1:
                        EnterFinWait2(flow, tcp_header, remote_timestamp);
                        /* no reply */
                        break;
                    case TcpFlow::State::kClosing:
                        EnterTimeWait(flow, tcp_header, remote_timestamp);
                        /* no reply */
                        break;
                    case TcpFlow::State::kLastAck:
                        EnterClosed(flow, tcp_header, remote_timestamp);
                        /* no reply */
                        /* TODO: raise event: connection closed */
                        tcp_shared_handle_->ReleaseFlow(flow);
                        break;
                    default:
                        return TcpInvalid(packet);
                }
            }
        }

        if(ret != nullptr) {
            ret->custom_value_ = reinterpret_cast<uint64_t>(flow);
            tcp_shared_handle_->AcquireFlow(flow);
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
