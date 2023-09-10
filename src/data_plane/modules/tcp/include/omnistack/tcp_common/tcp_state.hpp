//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_STATE_HPP
#define OMNISTACK_TCP_COMMON_TCP_STATE_HPP

#include <cstdint>
#include <omnistack/tcp_common/tcp_buffer.hpp>
#include <omnistack/tcp_common/tcp_congestion_control.hpp>
#include <omnistack/tcp_common/tcp_events.hpp>

namespace omnistack::data_plane::tcp_common {

    class TcpReceiveVariables {
    public:
        uint32_t irs_;              // initial receive sequence number
        uint32_t recv_nxt_;         // next sequence number expected on an incoming segments, and is the left or lower edge of the receive window
        uint16_t recv_wnd_;         // receive window
        uint8_t recv_wscale_;       // window scale
        uint8_t received_;          // if new packet is received since last ack sent
        uint32_t timestamp_recent_; // timestamp recently received
        TcpReceiveBuffer* receive_buffer_;
    };

    class TcpSendVariables {
    public:
        uint32_t send_una_;             // send unacknowledged
        uint32_t send_nxt_;             // next to be sent
        uint32_t send_wnd_;             // calculated send window
        uint32_t iss_;                  // initial send sequence number
        uint32_t send_wl1_;             // segment sequence number used for last window update
        uint32_t send_wl2_;             // segment acknowledgment number used for last window update
        TcpSendBuffer* send_buffer_;

        uint64_t rxtcur_;                   // retransmission timeout currently
        uint64_t srtt_;                     // smoothed round-trip time
        uint64_t rttvar_;                   // round-trip time variation
        uint64_t rto_begin_;                // retransmission timeout begin
        uint64_t rto_timeout_;              // retransmission timeout
        uint8_t is_retransmission_;         // if last packet is retransmission
        uint8_t fast_retransmission_;       // if fast retransmission is triggered
        uint8_t in_retransmission_queue_;   // if in sending queue
        uint8_t send_wscale_;               // window scale
        
    };

    class TcpListenFlow {
    public:
        uint32_t local_ip_;
        uint16_t local_port_;
    
        char* congestion_control_algorithm_;

        node::BasicNode* node_;                // node that this flow belongs to
    private:
        TcpListenFlow() = default;
        TcpListenFlow(const TcpListenFlow&) = default;
        TcpListenFlow(TcpListenFlow&&) = default;
        ~TcpListenFlow() = default;

        static TcpListenFlow* Create(memory::MemoryPool* flow_pool, uint32_t local_ip, uint16_t local_port, TcpListenOptions options);

        static void Destroy(memory::MemoryPool* flow_pool, TcpListenFlow* flow);

        friend class TcpSharedHandle;
    };

    class TcpFlow {
    public:
        enum class State : uint8_t {
            kClosed,
            kListen,
            kSynSent,
            kSynReceived,
            kEstablished,
            kFinWait1,
            kFinWait2,  
            kCloseWait,
            kClosing,
            kLastAck,
            kTimeWait
        };

        uint32_t local_ip_;
        uint32_t remote_ip_;
        uint16_t local_port_;
        uint16_t remote_port_;

        State state_;

        node::BasicNode* node_;                // node that this flow belongs to

        uint32_t reference_count_;  // reference count for this flow
        uint16_t mss_;              // maximum segment size

        TcpReceiveVariables receive_variables_;
        TcpSendVariables send_variables_;

        TcpCongestionControlBase* congestion_control_;
        
    private:
        TcpFlow() = default;
        TcpFlow(const TcpFlow&) = default;
        TcpFlow(TcpFlow&&) = default;
        ~TcpFlow() = default;

        static TcpFlow* Create(memory::MemoryPool* flow_pool, memory::MemoryPool* receive_buffer_pool_, memory::MemoryPool* send_buffer_pool_, uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);

        static void Destroy(memory::MemoryPool* flow_pool, memory::MemoryPool* receive_buffer_pool_, memory::MemoryPool* send_buffer_pool_, TcpFlow* flow);

        void Acquire();

        bool Release();

        friend class TcpSharedHandle;
    };

    inline TcpListenFlow* TcpListenFlow::Create(memory::MemoryPool* flow_pool, uint32_t local_ip, uint16_t local_port, TcpListenOptions options) {
        auto addr = flow_pool->Get();
        if(addr == nullptr) return nullptr;
        auto flow = new(addr) TcpListenFlow();
        flow->local_ip_ = local_ip;
        flow->local_port_ = local_port;
        flow->congestion_control_algorithm_ = options.congestion_control_algorithm_;
        return flow;
    }

    inline void TcpListenFlow::Destroy(memory::MemoryPool* flow_pool, TcpListenFlow* flow) {
        flow->~TcpListenFlow();
        flow_pool->Put(flow);
    }

    inline TcpFlow* TcpFlow::Create(memory::MemoryPool* flow_pool, memory::MemoryPool* receive_buffer_pool_, memory::MemoryPool* send_buffer_pool_, uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
        auto addr = flow_pool->Get();
        if(addr == nullptr) return nullptr;
        auto flow = new(addr) TcpFlow();
        flow->local_ip_ = local_ip;
        flow->remote_ip_ = remote_ip;
        flow->local_port_ = local_port;
        flow->remote_port_ = remote_port;
        flow->reference_count_ = 1;

        flow->receive_variables_.receive_buffer_ = TcpReceiveBuffer::Create(receive_buffer_pool_);
        flow->receive_variables_.received_ = 0;

        flow->send_variables_.send_buffer_ = TcpSendBuffer::Create(send_buffer_pool_);
        flow->send_variables_.in_retransmission_queue_ = 0;

        return flow;
    }

    inline void TcpFlow::Destroy(memory::MemoryPool* flow_pool, memory::MemoryPool* receive_buffer_pool_, memory::MemoryPool* send_buffer_pool_, TcpFlow* flow) {
        TcpReceiveBuffer::Destroy(receive_buffer_pool_, flow->receive_variables_.receive_buffer_);
        TcpSendBuffer::Destroy(send_buffer_pool_, flow->send_variables_.send_buffer_);
        flow->congestion_control_->~TcpCongestionControlBase();
        flow->~TcpFlow();
        flow_pool->Put(flow);
    }

    inline void TcpFlow::Acquire() {
        ++reference_count_;
    }

    inline bool TcpFlow::Release() {
        return --reference_count_ == 0;
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_STATE_HPP
