//
// Created by liuhao on 23-8-16.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/random.hpp>

namespace omnistack::data_plane::tcp_state_out {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpStateOut";

    class TcpStateOut : public Module<TcpStateOut, kName> {
    public:
        TcpStateOut() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* EventCallback(Event* event) override;

        std::vector<Event::EventType> RegisterEvents() override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
    private:
        Packet* OnConnect(Event* event);

        Packet* OnDisconnect(Event* event);

        void EnterSynSent(TcpFlow* flow, const char* congestion_control_algorithm);

        void EnterFinWait1(TcpFlow* flow);

        void EnterLastAck(TcpFlow* flow);

        void EnterClosed(TcpFlow* flow);

        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
    };

    bool TcpStateOut::DefaultFilter(Packet* packet) {
        return false;
    }

    std::vector<Event::EventType> TcpStateOut::RegisterEvents() {
        return std::vector<Event::EventType>{
                kTcpEventTypeConnect,
                kTcpEventTypeDisconnect
        };
    }

    Packet* TcpStateOut::EventCallback(Event* event) {
        switch (event->type_) {
            case kTcpEventTypeConnect:
                return OnConnect(event);
            case kTcpEventTypeDisconnect:
                return OnDisconnect(event);
            default:
                return nullptr;
        }
    }

    inline void TcpStateOut::EnterSynSent(TcpFlow* flow, const char* congestion_control_algorithm) {
        flow->mss_ = kTcpMaxSegmentSize;

        /* set receive variables */
        auto& recv_var = flow->receive_variables_;
        recv_var.recv_wnd_ = kTcpDefaultReceiveWindow;
#if defined (OMNI_TCP_OPTION_WSOPT)
        recv_var.recv_wscale_ = kTcpDefaultReceiveWindowScale;
#else
        recv_var.recv_wscale_ = 0;
#endif

        /* set send variables */
        auto& send_var = flow->send_variables_;
        send_var.iss_ = Rand32();
        send_var.send_una_ = send_var.iss_;
        send_var.send_nxt_ = send_var.iss_;
        send_var.send_wnd_ = kTcpDefaultSendWindow;
        send_var.send_wscale_ = 0;
        send_var.send_wl1_ = send_var.iss_ - 1;
        send_var.rxtcur_ = kTcpMinimumRetransmissionTimeout;
        send_var.rto_begin_ = 0;
        send_var.rto_timeout_ = 0;

        /* set congestion control algorithm */
        flow->congestion_control_ = TcpCongestionControlFactory::instance_().Create(congestion_control_algorithm, flow);

        flow->state_ = TcpFlow::State::kSynSent;
    }

    inline void TcpStateOut::EnterFinWait1(TcpFlow* flow) {
        flow->state_ = TcpFlow::State::kFinWait1;
    }

    inline void TcpStateOut::EnterLastAck(TcpFlow* flow) {
        flow->state_ = TcpFlow::State::kLastAck;
    }

    inline void TcpStateOut::EnterClosed(TcpFlow* flow) {
        flow->state_ = TcpFlow::State::kClosed;
    }

    Packet* TcpStateOut::OnConnect(Event* event) {
        auto tcp_event = static_cast<TcpEventConnect*>(event);
        uint32_t local_ip = tcp_event->local_ipv4_;
        uint32_t remote_ip = tcp_event->remote_ipv4_;
        uint16_t local_port = tcp_event->local_port_;
        uint16_t remote_port = tcp_event->remote_port_;

        if(tcp_shared_handle_->GetFlow(local_ip, remote_ip, local_port, remote_port) != nullptr) {
            /* TODO: report error? */
            return nullptr;
        }
        auto flow = tcp_shared_handle_->CreateFlow(local_ip, remote_ip, local_port, remote_port);
        if(flow == nullptr) return nullptr;
        flow->state_ = TcpFlow::State::kClosed;
        EnterSynSent(flow, kTcpDefaultCongestionControlAlgorithm);
        
        /* send SYN */
        auto packet = BuildReplyPacketWithFullOptions(flow, 0, packet_pool_);
        flow->send_variables_.send_nxt_ ++;

        packet->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);
        
        return packet;
    }

    Packet* TcpStateOut::OnDisconnect(Event* event) {
        auto tcp_event = static_cast<TcpEventDisconnect*>(event);
        uint32_t local_ip = tcp_event->local_ipv4_;
        uint32_t remote_ip = tcp_event->remote_ipv4_;
        uint16_t local_port = tcp_event->local_port_;
        uint16_t remote_port = tcp_event->remote_port_;

        auto flow = tcp_shared_handle_->GetFlow(local_ip, remote_ip, local_port, remote_port);
        if(flow == nullptr) return nullptr;
        if(flow->state_ == TcpFlow::State::kSynSent) {
            EnterClosed(flow);
            return nullptr;
        }
        else if(flow->state_ == TcpFlow::State::kSynReceived) EnterFinWait1(flow);
        else if(flow->state_ == TcpFlow::State::kEstablished) EnterFinWait1(flow);
        else if(flow->state_ == TcpFlow::State::kCloseWait) EnterLastAck(flow);
        else {
            /* TODO: report error? */
            return nullptr;
        }

        /* send FIN */
        auto packet = BuildReplyPacketWithFullOptions(flow, 0, packet_pool_);
        flow->send_variables_.send_nxt_ ++;

        packet->custom_value_ = reinterpret_cast<uint64_t>(flow);
        tcp_shared_handle_->AcquireFlow(flow);

        return packet;
    }

    void TcpStateOut::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpStateOut::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
