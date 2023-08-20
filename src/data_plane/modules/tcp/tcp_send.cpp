//
// Created by liuhao on 23-8-16.
//

#include <omnistack/tcp_common/tcp_shared.hpp>

namespace omnistack::data_plane::tcp_send {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpSend";

    class TcpSend : public Module<TcpSend, kName> {
    public:
        TcpSend() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* EventCallback(Event* event) override;

        std::vector<Event::EventType> RegisterEvents() override;

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        constexpr uint32_t max_burst_() override { return 1; }
    
    private:
        Packet* OnFastRetransmission(Event* event);

        struct FlowTimer {
            TcpFlow* flow;
            Packet* control_packet;
        };

        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_;
        std::queue<FlowTimer> flow_timers_;
    };

    bool TcpSend::DefaultFilter(Packet* packet) {
        return true;
    }

    inline Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    inline Packet* BuildDataPacket(TcpFlow* flow, uint32_t seq, Packet* ref_packet, PacketPool* packet_pool) {
        auto packet = packet_pool->Reference(ref_packet);

        /* build tcp header */
        auto& header_tcp = packet->packet_headers_[packet->header_tail_++];
        header_tcp.length_ = TcpHeaderLength(false, false, false, false, true);
        header_tcp.offset_ = 0;
        packet->data_ = packet->data_ - header_tcp.length_;
        packet->length_ += header_tcp.length_;
        auto tcp = reinterpret_cast<TcpHeader*>(packet->data_ + header_tcp.offset_);
        tcp->sport = flow->local_port_;
        tcp->dport = flow->remote_port_;
        tcp->seq = htonl(seq);
        tcp->ACK = htonl(flow->receive_variables_.recv_nxt_);
        tcp->tcpflags = TCP_FLAGS_ACK | TCP_FLAGS_PSH;
        tcp->dataofs = header_tcp.length_ >> 2;
        tcp->window = htons(flow->receive_variables_.recv_wnd_);
        tcp->urgptr = 0;

        /* set tcp options */
        auto tcp_options = reinterpret_cast<uint8_t*>(tcp) + sizeof(TcpHeader);
#if defined (OMNI_TCP_OPTION_TSPOT)
        if(flow->receive_variables_.timestamp_recent_ != 0) {
            *tcp_options = TCP_OPTION_KIND_NOP;
            *(tcp_options + 1) = TCP_OPTION_KIND_NOP;
            *(tcp_options + 2) = TCP_OPTION_KIND_TSPOT;
            *(tcp_options + 3) = TCP_OPTION_LENGTH_TSPOT;
            *reinterpret_cast<uint32_t*>(tcp_options + 4) = htonl(static_cast<uint32_t>(NowMs()));
            *reinterpret_cast<uint32_t*>(tcp_options + 8) = htonl(flow->receive_variables_.timestamp_recent_);
            tcp_options = tcp_options + 12;
        }
#endif

        /* build ipv4 header */
        auto& header_ipv4 = packet->packet_headers_[packet->header_tail_++];
        header_ipv4.length_ = sizeof(Ipv4Header);
        header_ipv4.offset_ = 0;
        header_tcp.offset_ = header_tcp.offset_ + header_ipv4.length_;
        packet->data_ = packet->data_ - header_ipv4.length_;
        packet->length_ += header_ipv4.length_;
        auto ipv4 = reinterpret_cast<Ipv4Header*>(packet->data_ + header_ipv4.offset_);
        ipv4->version = 4;
        ipv4->proto = IP_PROTO_TYPE_TCP;
        ipv4->src = flow->local_ip_;
        ipv4->dst = flow->remote_ip_;

        return packet;
    }

    Packet* TcpSend::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
        if(flow == nullptr) return TcpInvalid(packet);
        auto& send_var = flow->send_variables_;

        Packet* ret = nullptr;

        if(packet->header_tail_ == 0) {
            /* pure data */
            /* check if in valid state */
            if(flow->state_ > TcpFlow::State::kEstablished || flow->state_ < TcpFlow::State::kSynSent)
                return TcpInvalid(packet);

            /* check if can send immediately */
            /* get bytes can send from congestion control */
            uint32_t bytes_can_send = flow->congestion_control_->GetBytesCanSend();
            if(!send_var.send_buffer_->EmptyUnsent() && bytes_can_send >= packet->length_) {
                /* send packet */
                ret = BuildDataPacket(flow, send_var.send_nxt_, packet, packet_pool_);
                send_var.send_nxt_ += packet->length_;
                send_var.send_buffer_->PushSent(packet);

                /* update congestion control */
                flow->congestion_control_->OnPacketSent(packet->length_);
            }
            else {
                /* store data in send buffer */
                send_var.send_buffer_->PushUnsent(packet);
            }
            
            /* set retransmission timer */
            if(!flow->send_variables_.in_retransmission_queue_) {
                send_var.is_retransmission_ = 0;
                send_var.rto_begin_ = NowUs();
                send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                flow_timers_.push(FlowTimer(flow, nullptr));
                tcp_shared_handle_->AcquireFlow(flow);
                flow->send_variables_.in_retransmission_queue_ ++;
            }
        }
        else {
            /* control packet */
            if(flow->state_ == TcpFlow::State::kSynSent ||
               flow->state_ == TcpFlow::State::kSynReceived ||
               flow->state_ == TcpFlow::State::kFinWait1 ||
               flow->state_ == TcpFlow::State::kLastAck ||
               flow->state_ == TcpFlow::State::kClosing) {
                /* set retransmission timer */
                send_var.rto_begin_ = NowUs();
                send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                flow_timers_.push(FlowTimer(flow, packet));
                tcp_shared_handle_->AcquireFlow(flow);
                flow->send_variables_.in_retransmission_queue_ ++;
            
                /* send packet */
                ret = packet_pool_->Duplicate(packet);
            }
        }

        return ret;
    }

    Packet* TcpSend::TimerLogic(uint64_t tick) {
        Packet* ret = nullptr;
        while(!flow_timers_.empty()) {
            auto flow_timer = flow_timers_.front();
            flow_timers_.pop();
            auto flow = flow_timer.flow;
            auto control_packet = flow_timer.control_packet;
            auto& send_var = flow->send_variables_;
            if(control_packet != nullptr) [[unlikely]] {
                if(flow->state_ == TcpFlow::State::kSynSent ||
                   flow->state_ == TcpFlow::State::kSynReceived) {
                    /* set retransmission timer */
                    send_var.rxtcur_ <<= 1;
                    send_var.rto_begin_ = NowUs();
                    send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                    flow_timers_.push(FlowTimer(flow, control_packet));

                    /* retransmit SYN packet */
                    auto packet = packet_pool_->Duplicate(control_packet);
                    packet->next_packet_ = ret;
                    ret = packet;
                }
                else if(flow->state_ == TcpFlow::State::kFinWait1 ||
                        flow->state_ == TcpFlow::State::kLastAck ||
                        flow->state_ == TcpFlow::State::kClosing) {
                    if(send_var.in_retransmission_queue_ > 1) {
                        /* set retransmission timer */
                        send_var.rto_begin_ = NowUs();
                        send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                        flow_timers_.push(FlowTimer(flow, control_packet));
                    }
                    else {
                        /* set retransmission timer */
                        send_var.rxtcur_ <<= 1;
                        send_var.rto_begin_ = NowUs();
                        send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                        flow_timers_.push(FlowTimer(flow, control_packet));

                        /* retransmit FIN packet */
                        auto packet = packet_pool_->Duplicate(control_packet);
                        packet->next_packet_ = ret;
                        ret = packet;
                    }
                }
                else {
                    /* invalid state */
                    send_var.in_retransmission_queue_ --;
                    tcp_shared_handle_->ReleaseFlow(flow);
                    control_packet->Release();
                }
            }
            else {
                /* check if retransmission required */
                if(!send_var.rto_timeout_) [[unlikely]] {
                    if(send_var.send_buffer_->EmptyUnsent()) {
                        /* no data to send */
                        send_var.in_retransmission_queue_ --;
                        tcp_shared_handle_->ReleaseFlow(flow);
                    }
                    else {
                        /* set retransmission timer */
                        send_var.is_retransmission_ = 0;
                        send_var.rto_begin_ = NowUs();
                        send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                        flow_timers_.push(FlowTimer(flow, nullptr));
                    }
                }
                else {
                    /* check fast retransmission */
                    if(send_var.fast_retransmission_) [[unlikely]] {
                        /* set retransmission timer */
                        send_var.fast_retransmission_ = 0;
                        send_var.rto_begin_ = NowUs();
                        send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                        flow_timers_.push(FlowTimer(flow, nullptr));

                        /* retransmit packet */
                        auto packet = BuildDataPacket(flow, send_var.send_una_, send_var.send_buffer_->FrontSent(), packet_pool_);
                        packet->next_packet_ = ret;
                        ret = packet;

                        /* update congestion control */
                        flow->congestion_control_->OnPacketSent(send_var.send_buffer_->FrontSent()->length_);
                    }
                    else if(tick >= send_var.rto_timeout_) [[unlikely]] {
                        /* update congestion control */
                        flow->congestion_control_->OnRetransmissionTimeout();

                        /* set retransmission timer */
                        send_var.is_retransmission_ ++;
                        send_var.rxtcur_ <<= 1;
                        send_var.rto_begin_ = NowUs();
                        send_var.rto_timeout_ = send_var.rto_begin_ + send_var.rxtcur_;
                        flow_timers_.push(FlowTimer(flow, nullptr));

                        /* retransmit packet */
                        auto packet = BuildDataPacket(flow, send_var.send_una_, send_var.send_buffer_->FrontSent(), packet_pool_);
                        packet->next_packet_ = ret;
                        ret = packet;

                        /* update congestion control */
                        flow->congestion_control_->OnPacketSent(send_var.send_buffer_->FrontSent()->length_);
                    }
                }

                /* check if new data can be sent */
                /* get bytes can send from congestion control */
                uint32_t bytes_can_send = flow->congestion_control_->GetBytesCanSend();
                while(!send_var.send_buffer_->EmptyUnsent()) [[likely]] {
                    auto packet = send_var.send_buffer_->FrontUnsent();
                    if(packet->length_ > bytes_can_send) break;
                    send_var.send_buffer_->PopUnsent();

                    /* send packet */
                    auto tmp = BuildDataPacket(flow, send_var.send_nxt_, packet, packet_pool_);
                    tmp->next_packet_ = ret;
                    ret = tmp;

                    send_var.send_nxt_ += packet->length_;
                    send_var.send_buffer_->PushSent(packet);
                    bytes_can_send -= packet->length_;

                    /* update congestion control */
                    flow->congestion_control_->OnPacketSent(packet->length_);
                }
            }
        }
        return ret;
    }

    void TcpSend::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpSend::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
