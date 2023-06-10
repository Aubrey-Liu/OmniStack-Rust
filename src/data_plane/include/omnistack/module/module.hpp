//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_HPP
#define OMNISTACK_MODULE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <omnistack/graph/steer_info.hpp>

namespace omnistack {
    namespace data_plane {
        class PacketPool;
        enum class ModuleType {
            kModuleTypeReadOnly = 0,
            kModuleTypeReadWrite,
            kModuleTypeOccupy
        };

        class Module {
        public:
            static constexpr bool DefaultFilter(DataPlanePacket& packet) { return true; }

            virtual std::function<bool(DataPlanePacket& packet)> GetFilter() { return DefaultFilter; };

            virtual DataPlanePacket* MainLogic(DataPlanePacket* packet) { return packet; }

            virtual DataPlanePacket* TimerLogic(uint64_t tick) { return nullptr; }

            virtual void Init(const std::string& name_prefix, PacketPool& packet_pool) {};

            virtual void Destroy() {};

            virtual constexpr bool allow_duplication_() { return true; }

            ModuleType type_;
            uint16_t burst_;
            const std::string name_;
        };
    }
}

#endif //OMNISTACK_MODULE_HPP
