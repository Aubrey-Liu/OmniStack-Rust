//
// Created by liuhao on 23-8-11.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/module/module.hpp>

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
        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_;
    };

    bool TcpStateIn::DefaultFilter(Packet* packet) {
        return true;
    }

    static inline
    Packet* TcpInvalid(Packet* packet) {
        packet->Release();
        return nullptr;
    }

    Packet* TcpStateIn::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
        auto tcp_header = reinterpret_cast<TcpHeader*>((packet->header_tail_ - 1)->data_);
        auto ipv4_header = reinterpret_cast<Ipv4Header*>((packet->header_tail_ - 2)->data_);
        
        uint32_t local_ip = ipv4_header->dst;
        uint32_t remote_ip = ipv4_header->src;
        uint16_t local_port = tcp_header->dport;
        uint16_t remote_port = tcp_header->sport;

        if(tcp_header->rst) [[unlikely]] {
            /* TODO: handle reset */    
        }

        if(tcp_header->syn) [[unlikely]] {
            if(!tcp_header->ack) {
                /* handle SYN */
                switch (flow->state_) {
                    case TcpFlow::State::kListen: {
                        // flow = 
                    }
                    case TcpFlow::State::kSynSent: {

                    }
                    default:
                        return TcpInvalid(packet);
                }
            }
            else {
                /* handle SYN-ACK */
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
    }

    void TcpStateIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpStateIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
