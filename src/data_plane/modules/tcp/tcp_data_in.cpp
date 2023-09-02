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

        Filter GetFilter(uint32_t upstream_module, uint32_t global_id) override { return DefaultFilter; }

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
        constexpr bool has_timer_() override { return true; }

    private:
        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
        TcpFlow* ack_list_[kTcpMaxFlowCount];
        uint32_t ack_list_top_;

        uint32_t next_hop_filter_mask_data_;
        uint32_t next_hop_filter_mask_ack_;
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
        tcp_shared_handle_->ReleaseFlow(flow);
        auto tcp_header = packet->GetL4Header<TcpHeader>();

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
                packet = recv_var.receive_buffer_->Pop(seq_end, packet);
                ret = packet;
                recv_var.recv_nxt_ = seq_end;
            }
            else recv_var.receive_buffer_->Push(seq_num, packet);
            if(recv_var.receive_buffer_->size_() > 10000)
                OMNI_LOG_TAG(kWarning, "TCP_DATA_IN") << "receive buffer size: " << recv_var.receive_buffer_->size_() << "\n";
        }
        else packet->Release();

        /* set node info and forward info */
        auto iter = ret;
        while(iter != nullptr) {
            iter->node_ = flow->node_;
            iter->next_hop_filter_ &= next_hop_filter_mask_data_;
            iter = iter->next_packet_.Get();
        }

        /* check if ack immediately */
        if(ret == nullptr || ret->next_packet_.Get() != nullptr) [[unlikely]] {
            /* ack immediately */
            auto ack_packet = BuildReplyPacket(flow, TCP_FLAGS_ACK, packet_pool_);
            /* set forward info */
            ack_packet->next_hop_filter_ &= next_hop_filter_mask_ack_;

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
            /* set forward info */
            ack_packet->next_hop_filter_ = next_hop_filter_mask_ack_;

            ack_packet->next_packet_ = ret;
            ret = ack_packet;
        }
        return ret;
    }

    void TcpDataIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
        ack_list_top_ = 0;

        /* set next hop filter mask */
        uint32_t node_user_mask = 0;
        uint32_t ipv4_sender_mask = 0;
        uint32_t universe_mask = 0;
        for(auto son : downstream_nodes_) {
            universe_mask |= son.filter_mask;
            if(son.module_type == ConstCrc32("NodeUser")) node_user_mask |= son.filter_mask;
            else if(son.module_type == ConstCrc32("Ipv4Sender")) ipv4_sender_mask |= son.filter_mask;
        }
        next_hop_filter_mask_ack_ = ~node_user_mask & universe_mask;
        next_hop_filter_mask_data_ = ~ipv4_sender_mask & universe_mask;
    }

    void TcpDataIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
