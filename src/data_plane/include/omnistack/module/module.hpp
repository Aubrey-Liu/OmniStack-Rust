//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_HPP
#define OMNISTACK_MODULE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <concepts>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <omnistack/graph/steer_info.hpp>

namespace omnistack::data_plane {
    class PacketPool;

    class BaseModule {
    public:
        typedef std::function<bool(DataPlanePacket* packet)> Filter;

        enum class ModuleType {
            kReadOnly = 0,
            kReadWrite,
            kOccupy
        };

        enum class FilterGroupType {
            kMutex = 0,
            kEqual
        };

        BaseModule() = default;

        static constexpr bool DefaultFilter(DataPlanePacket* packet){ return true; }

        void RegisterDownstreamFilters(const std::vector<Filter>& filters, const std::vector<uint32_t>& filter_masks, const std::vector<std::set<uint32_t>>& groups, const std::vector<FilterGroupType>& group_types);

        void set_upstream_nodes_(const std::vector<std::pair<std::string, uint32_t>>& upstream_nodes);

        void ApplyDownstreamFilters(DataPlanePacket* packet);

        virtual Filter GetFilter(std::string_view upstream_module, uint32_t global_id) { return DefaultFilter; };

        virtual DataPlanePacket* MainLogic(DataPlanePacket* packet) { return packet; }

        virtual DataPlanePacket* TimerLogic(uint64_t tick) { return nullptr; }

        virtual void Init(std::string_view name_prefix, const std::shared_ptr<PacketPool>& packet_pool) {};

        virtual void Destroy() {};

        static constexpr bool allow_duplication_() { return false; }

        /* TODO: use static_assert to check this */
        static constexpr std::string_view name_() { return "BaseModule"; }

        static constexpr ModuleType type_() { return ModuleType::kOccupy; }

        /* when does this act? will it be done in son-class? */
        uint32_t burst_ = 1;

        /* seems that this will introduce an extra function-call by virtual, only useful when visiting through son-class pointer */
//        virtual constexpr ModuleType type_() { return ModuleType::kOccupy; }
//
//        virtual constexpr uint32_t burst_() { return 1; }

    private:
        struct FilterGroup {
            std::vector<Filter> filters_;
            std::vector<uint32_t> filter_masks_;
            uint32_t universe_mask_;
            FilterGroupType type_;
            uint8_t last_apply_;
        };

        std::vector<FilterGroup> filter_groups_;
        std::vector<std::pair<std::string, uint32_t>> upstream_nodes_;
    };

    class ModuleFactory {
    public:
        typedef std::function<std::unique_ptr<BaseModule>()> CreateFunction;

        static ModuleFactory& instance() {
            static ModuleFactory factory;
            return factory;
        }

        void Register(const std::string& name, const CreateFunction& func) {
            if(module_list_.find(name) != module_list_.end()) {
                /* TODO: report error */
            }
            if(func == nullptr) {
                /* TODO: report error */
            }
            if(!module_list_.insert(std::make_pair(name, func)).second) {
                /* TODO: report error */
            }
        }

        [[nodiscard]] std::unique_ptr<BaseModule> Create(const std::string& name) const {
            auto it = module_list_.find(name);
            if(it == module_list_.end()) {
                /* TODO: report error */
                return nullptr;
            }
            return it->second();
        }

    private:
        std::map<std::string, CreateFunction> module_list_;
    };

    template<typename T, const char name[]>
    class Module : public BaseModule {
    public:
        static std::unique_ptr<BaseModule> CreateModuleObject() {
            return std::make_unique<BaseModule>(new T());
        }

        static constexpr std::string_view name_() { return std::string_view(name); }

    private:
        struct FactoryEntry {
            FactoryEntry() {
                ModuleFactory::instance().Register(std::string(name), CreateModuleObject);
            }
        };

        static const FactoryEntry factory_entry_;
    };

    template<typename T, const char name[]>
    const typename Module<T, name>::FactoryEntry Module<T, name>::factory_entry_;
}

#endif //OMNISTACK_MODULE_HPP
