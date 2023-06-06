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
        enum ModuleType {
            MODULE_TYPE_READ_ONLY = 0,
            MODULE_TYPE_READ_WRITE,
            MODULE_TYPE_OCCUPY
        };

        class Module {
        public:
            static constexpr bool default_filter(Packet* packet) { return true; }

            virtual std::function<bool(Packet* packet)> get_filter() { return default_filter; };

            virtual constexpr bool m_allow_duplication() { return true; }

            virtual Packet* main_logic(Packet* packet) { return packet; }

            virtual Packet* timer_logic(uint64_t tick);

            virtual void init(const std::string &name_prefix, packet_pool);
            virtual void destroy();

            ModuleType m_type;
            uint16_t m_burst;
            std::string m_name;
        };
    }
}

#endif //OMNISTACK_MODULE_HPP
