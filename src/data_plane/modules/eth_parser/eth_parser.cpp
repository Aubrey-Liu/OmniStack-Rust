//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

namespace omnistack::data_plane::eth_parser {

    using namespace omnistack::common;

    inline constexpr char kName[] = "EthParser";

    class EthParser : public Module<EthParser, kName> {
    public:
        EthParser() {}

        DataPlanePacket* MainLogic(DataPlanePacket* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kReadWrite; }
    };

    DataPlanePacket* EthParser::MainLogic(DataPlanePacket* packet) {
        EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(packet->data_ + packet->offset_);
        PacketHeader &eth = *(packet->header_tail_ ++);
        eth.length_ = sizeof(EthernetHeader);
        eth.data_ = reinterpret_cast<unsigned char*>(eth_header);
        packet->offset_ += eth.length_;
        return packet;
    }

}