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

        Packet* MainLogic(Packet* packet) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        void Destroy() override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }
    
    private:
        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_; 
    };

    bool TcpStateOut::DefaultFilter(Packet* packet) {
        return true;
    }

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    Packet* TcpStateOut::MainLogic(Packet* packet) {
    }

    void TcpStateOut::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpStateOut::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
