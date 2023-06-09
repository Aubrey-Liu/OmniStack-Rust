//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_HPP
#define OMNISTACK_MODULE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace omnistack {
    namespace data_plane {
        class Packet;
        class PacketPool;
        enum class ModuleType {
            kReadOnly = 0,
            kReadWrite,
            kOccupy
        };

        class Module {
        public:
            static constexpr bool DefaultFilter(Packet* packet) { return true; }

            virtual std::function<bool(Packet* packet)> GetFilter() { return DefaultFilter; };

            virtual Packet* MainLogic(Packet* packet) { return packet; }

            virtual Packet* TimerLogic(uint64_t tick);

            virtual void Init(const std::string &name_prefix, PacketPool* packet_pool);

            virtual void Destroy();

            virtual constexpr bool allow_duplication_() { return true; }

            ModuleType type_;
            uint16_t burst_;
            const std::string name_;
        };
    }
}

#endif //OMNISTACK_MODULE_HPP
