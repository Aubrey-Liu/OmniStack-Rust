//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::eth_recver {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "EthRecver";

    class EthRecver : public Module<EthRecver, kName> {
    public:
        EthRecver() {}

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    Packet* EthRecver::MainLogic(Packet* packet) {
        EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(packet->data_ + packet->offset_);
        auto& eth = packet->l2_header;
        eth.length_ = sizeof(EthernetHeader);
        eth.offset_ = packet->offset_;
        packet->offset_ += eth.length_;
        return packet;
    }

}