//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_HPP
#define OMNISTACK_MODULE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <omnistack/graph/steer_info.hpp>

namespace omnistack::data_plane {
    class PacketPool;

    typedef std::function<bool(DataPlanePacket& packet)> Filter;

    enum class ModuleType {
        kReadOnly = 0,
        kReadWrite,
        kOccupy
    };

    enum class FilterGroupType {
        kMutex = 0,
        kEqual
    };

    class Module {
    public:
        Module();
        Module(Module&&);

        static constexpr bool DefaultFilter(DataPlanePacket& packet) { return true; }

        void RegisterDownstreamFilters(const std::vector<Filter>& filters, const std::vector<uint32_t>& filter_masks, const std::vector<uint32_t>& group_ids, const std::vector<FilterGroupType>& group_types);

        void ApplyDownstreamFilters(DataPlanePacket& packet);

        virtual Filter GetFilter(const std::string& upstream_module) { return DefaultFilter; };

        virtual DataPlanePacket* MainLogic(DataPlanePacket* packet) { return packet; }

        virtual DataPlanePacket* TimerLogic(uint64_t tick) { return nullptr; }

        virtual void Init(const std::string& name_prefix, PacketPool& packet_pool) {};

        virtual void Destroy() {};

        virtual constexpr bool allow_duplication_() { return true; }

        ModuleType type_;
        uint16_t burst_;
        const std::string name_;

    private:
        struct FilterGroup {
            std::vector<Filter> filters;
            std::vector<uint32_t> filter_masks;
            uint32_t universe_mask;
            FilterGroupType type;
            uint8_t last_apply;
        };

        std::vector<FilterGroup> filter_groups_;
    };
}

#endif //OMNISTACK_MODULE_HPP
