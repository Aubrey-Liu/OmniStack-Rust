//
// Created by liuhao on 23-8-17.
//

#include <omnistack/tcp_common/tcp_shared.hpp>

namespace omnistack::data_plane::tcp_data_in {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpDataIn";

    class TcpDataIn : public Module<TcpDataIn, kName> {
    public:
        TcpDataIn() {}

        static bool DefaultFilter(Packet* packet);

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
    private:
        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
        TcpFlow* ack_list_[kTcpMaxFlowCount];
        uint32_t ack_list_top_;
    };

    bool TcpDataIn::DefaultFilter(Packet* packet) {
        return true;
    }

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    inline bool TcpLessUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) < 0;
    }

    inline Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    Packet* TcpDataIn::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
        if(flow == nullptr) return TcpInvalid(packet);
        auto tcp_header = reinterpret_cast<TcpHeader*>(packet->data_ + packet->packet_headers_[packet->header_tail_ - 1].offset_);

        auto& recv_var = flow->receive_variables_;
        uint32_t seq_num = ntohl(tcp_header->seq);
        Packet* ret = nullptr;

        /* clamp data */
        if(TcpLessUint32(seq_num, recv_var.recv_nxt_)) [[unlikely]] {
            if(TcpGreaterUint32(seq_num + packet->length_ - packet->offset_, recv_var.recv_nxt_)) {
                /* receive duplicated data with new data */
                packet->offset_ += recv_var.recv_nxt_ - seq_num;
                seq_num = recv_var.recv_nxt_;
            }
            else {
                /* receive duplicated data without new data */
                packet->offset_ = packet->length_;
            }
        }
        
        /* assembling data */
        uint32_t data_length = packet->length_ - packet->offset_;
        if(data_length > 0) [[likely]] {
            if(seq_num == recv_var.recv_nxt_) [[likely]] {
                uint32_t seq_end = seq_num + data_length;
                packet->next_packet_ = recv_var.receive_buffer_->Pop(seq_end);
                ret = packet;
                recv_var.recv_nxt_ = seq_end;
            }
            else recv_var.receive_buffer_->Push(seq_num, packet);
        }

        /* TODO: set node info and forward info */
        auto iter = ret;
        while(iter != nullptr) {
            iter->node_ = flow->node_;
            iter = iter->next_packet_.Get();
        }

        /* check if ack immediately */
        if(ret == nullptr || ret->next_packet_.Get() != nullptr) [[unlikely]] {
            /* ack immediately */
            auto ack_packet = BuildReplyPacket(flow, TCP_FLAGS_ACK, packet_pool_);
            /* TODO: set forward info */
            ack_packet->next_packet_ = ret;
            ret = ack_packet;
        }
        else if(!recv_var.received_) {
            recv_var.received_ = true;
            ack_list_[ack_list_top_++] = flow;
        }

        return ret;
    }

    Packet* TcpDataIn::TimerLogic(uint64_t tick) {
        if(ack_list_top_ == 0) return nullptr;
        Packet* ret = nullptr;
        while(ack_list_top_) {
            auto flow = ack_list_[--ack_list_top_];
            flow->receive_variables_.received_ = false;
            auto ack_packet = BuildReplyPacket(flow, TCP_FLAGS_ACK, packet_pool_);
            /* TODO: set forward info */
            ack_packet->next_packet_ = ret;
            ret = ack_packet;
        }
        return ret;
    }

    void TcpDataIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
        ack_list_top_ = 0;
    }

    void TcpDataIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
