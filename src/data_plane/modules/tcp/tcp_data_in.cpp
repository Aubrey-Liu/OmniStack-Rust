//
// Created by liuhao on 23-8-11.
//

#include <omnistack/tcp_common/tcp_shared.hpp>
#include <omnistack/common/random.hpp>

namespace omnistack::data_plane::tcp_data_in {
    using namespace tcp_common;

    inline constexpr char kName[] = "TcpDataIn";

    class TcpDataIn : public Module<TcpDataIn, kName> {
    public:
        TcpDataIn() {}

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

    bool TcpDataIn::DefaultFilter(Packet* packet) {
        return true;
    }

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    Packet* TcpDataIn::MainLogic(Packet* packet) {
        
    }

    void TcpDataIn::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        tcp_shared_handle_ = TcpSharedHandle::Create(name_prefix);
        packet_pool_ = packet_pool;
    }

    void TcpDataIn::Destroy() {
        TcpSharedHandle::Destroy(tcp_shared_handle_);
    }

}
