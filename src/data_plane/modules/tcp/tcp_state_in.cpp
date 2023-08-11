//
// Created by liuhao on 23-8-11.
//

#include <tcp_shared.hpp>
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

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    
    private:
        TcpSharedHandle* tcp_shared_handle_;
        PacketPool* packet_pool_;
    };

    bool TcpStateIn::DefaultFilter(Packet* packet) {
        return true;
    }

    Packet* TcpStateIn::MainLogic(Packet* packet) {
        auto flow = reinterpret_cast<TcpFlow*>(packet->custom_value_);
    }

    void TcpStateIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpStateIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
