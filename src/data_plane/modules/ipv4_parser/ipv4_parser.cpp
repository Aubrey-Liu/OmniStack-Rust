//
// Created by liuhao on 23-8-1.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane::ipv4_parser {

inline constexpr char kName[] = "Ipv4Parser";

class Ipv4Parser : public Module<Ipv4Parser, kName> {
public:
    static bool DefaultFilter(DataPlanePacket* packet);

    Filter GetFilter(std::string_view upstream_node, uint32_t global_id) override;

    DataPlanePacket* MainLogic(DataPlanePacket* packet) override;

    constexpr bool allow_duplication_() override { return true; }

    constexpr ModuleType type_() override { return ModuleType::kReadOnly; }
};

}
