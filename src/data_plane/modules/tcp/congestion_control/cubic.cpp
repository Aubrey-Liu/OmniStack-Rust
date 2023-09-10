//
// Created by liuhao on 23-8-19.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/logger.h>
#include <cmath>

namespace omnistack::data_plane::tcp_common::cubic {

    inline constexpr char kName[] = "Cubic";

    constexpr uint32_t kConstantCU = 2;
    constexpr uint32_t kConstantCV = 5;
    constexpr uint32_t kConstantBetaU = 7;
    constexpr uint32_t kConstantBetaV = 10;

    class Cubic : public TcpCongestionControl<Cubic, kName> {
    public:
        Cubic(TcpFlow* flow);

        void OnPacketAcked(uint32_t bytes) override;

        void OnRetransmissionTimeout() override;

        uint32_t GetCongestionWindow() const override { return cwnd_; }

        uint32_t GetBytesCanSend() const override;
    
    private:
        uint32_t IncreaseWindow(uint64_t t);

        uint32_t EstimateWindow(uint64_t t, uint64_t rtt);

        void UpdateKValue();

        enum class State {
            kSlowStart,
            kCongestionAvoidance,
            kFastRecovery
        };

        State state_;
        uint32_t cwnd_;
        uint32_t ssthresh_;
        uint32_t recovery_;
        uint32_t recovery_end_;
        uint32_t recovery_bonus_;
        uint32_t w_max_;
        double k_value_;
        uint64_t avoidance_tick_;
        uint64_t duplicated_ack_count_;
    };

    Cubic::Cubic(TcpFlow* flow) : TcpCongestionControl(flow) {
        flow->send_variables_.fast_retransmission_ = false;
        state_ = State::kSlowStart;
        if(kTcpMaxSegmentSize > 2190) cwnd_ = kTcpMaxSegmentSize * 2;
        else if(kTcpMaxSegmentSize > 1095) cwnd_ = kTcpMaxSegmentSize * 3;
        else cwnd_ = kTcpMaxSegmentSize * 4;
        ssthresh_ = UINT32_MAX >> 1;
        recovery_ = flow->send_variables_.iss_;
        recovery_end_ = recovery_;
        recovery_bonus_ = 0;
        w_max_ = cwnd_;
        k_value_ = 0;
        avoidance_tick_ = 0;
        duplicated_ack_count_ = 0;
    }

    inline uint32_t Cubic::IncreaseWindow(uint64_t t) {
        double x = t / 1000000.0 - k_value_;
        double w_cubic = x * x * x * kConstantCU / kConstantCV;
        return static_cast<uint32_t>(floor(w_cubic + w_max_));
    }

    inline uint32_t Cubic::EstimateWindow(uint64_t t, uint64_t rtt) {
        constexpr double a = 1.0 * kConstantBetaU / kConstantBetaV;
        constexpr double b = 3.0 * (kConstantBetaV - kConstantBetaU) * (kConstantBetaV + kConstantBetaU) / kConstantBetaV / kConstantBetaV;
        return a * w_max_ + b * t / rtt;
    }

    inline void Cubic::UpdateKValue() {
        constexpr double a = 1.0 * (kConstantBetaV - kConstantBetaU) * kConstantCU / kConstantBetaV / kConstantCV;
        k_value_ = pow(w_max_ * a, 1.0 / 3);
    }

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return static_cast<int32_t>(a - b) > 0;
    }

    void Cubic::OnPacketAcked(uint32_t bytes) {
        if(!bytes) [[unlikely]] {
            /* duplicated ACK */
            if(state_ == State::kFastRecovery) {
                cwnd_ += flow_->mss_;
                // if(cwnd_ > w_max_) {
                //     state_ = State::kCongestionAvoidance;
                //     avoidance_tick_ = NowUs();
                //     UpdateKValue();
                // }
            }
            else {
                ++ duplicated_ack_count_;
                if(duplicated_ack_count_ == 3) {
                    state_ = State::kFastRecovery;
                    if(w_max_ > cwnd_) cwnd_ = cwnd_ * (kConstantBetaU + kConstantBetaV) / kConstantBetaV / 2;
                    else w_max_ = cwnd_;
                    cwnd_ = cwnd_ * kConstantBetaU / kConstantBetaV;
                    ssthresh_ = std::max(cwnd_, flow_->mss_ * 2U);
                    recovery_ = flow_->send_variables_.send_nxt_;
                    recovery_end_ = recovery_;
                    flow_->send_variables_.fast_retransmission_ = 1;
                    OMNI_LOG_TAG(common::kDebug, "Cubic") << "trigger fast retransmission, snd_una = " << flow_->send_variables_.send_una_ << ", recovery = " << recovery_ << "\n";
                }
            }
        }
        else {
            switch (state_) {
                case State::kSlowStart:
                    duplicated_ack_count_ = 0;
                    cwnd_ += std::min(bytes, static_cast<uint32_t>(flow_->mss_));
                    if(cwnd_ >= ssthresh_) [[unlikely]] {
                        state_ = State::kCongestionAvoidance;
                        avoidance_tick_ = NowUs();
                        w_max_ = cwnd_;
                        k_value_ = 0;
                    }
                    break;
                case State::kCongestionAvoidance: {
                    duplicated_ack_count_ = 0;
                    uint64_t t_delta = NowUs() - avoidance_tick_;
                    uint32_t w_cubic = IncreaseWindow(t_delta);
                    uint32_t w_est = EstimateWindow(t_delta, flow_->send_variables_.rxtcur_);
                    if(w_cubic < w_est) cwnd_ = w_est;
                    else cwnd_ = IncreaseWindow(t_delta + flow_->send_variables_.rxtcur_);
                    break;
                }
                case State::kFastRecovery:
                    if(TcpGreaterUint32(flow_->send_variables_.send_una_, recovery_)) {
                        duplicated_ack_count_ = 0;
                        recovery_end_ = flow_->send_variables_.send_nxt_ + 3 * flow_->mss_;
                        state_ = State::kCongestionAvoidance;
                        cwnd_ = std::min(ssthresh_, std::max(flow_->send_variables_.send_nxt_ - flow_->send_variables_.send_una_, static_cast<uint32_t>(flow_->mss_)) + flow_->mss_);
                        avoidance_tick_ = NowUs();
                        UpdateKValue();
                    }
                    else {
                        flow_->send_variables_.fast_retransmission_ = 1;
                        uint32_t deflate = cwnd_ > bytes ? cwnd_ - bytes : 0;
                        cwnd_ = std::max(deflate, static_cast<uint32_t>(flow_->mss_));
                        if(bytes > flow_->mss_) cwnd_ += flow_->mss_;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void Cubic::OnRetransmissionTimeout() {
        ssthresh_ = std::max(cwnd_ * kConstantBetaU / kConstantBetaV, 2U * flow_->mss_);
        cwnd_ = flow_->mss_;
        recovery_ = flow_->send_variables_.send_nxt_;
        state_ = State::kSlowStart;
        duplicated_ack_count_ = 0;
    }

    uint32_t Cubic::GetBytesCanSend() const {
        uint32_t inflight = flow_->send_variables_.send_nxt_ - flow_->send_variables_.send_una_;
        uint32_t send_wnd = flow_->send_variables_.send_wnd_;
        uint32_t wnd = std::min(send_wnd, cwnd_);
        if(duplicated_ack_count_ <= 2) wnd += flow_->mss_;
        if(duplicated_ack_count_ == 2) wnd += flow_->mss_;
        uint32_t ret = inflight < wnd ? wnd - inflight : 0;
        if(TcpGreaterUint32(recovery_end_, flow_->send_variables_.send_nxt_))
            ret += recovery_end_ - flow_->send_variables_.send_nxt_;
        return ret;
    }

}