//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_MODULE_HPP
#define OMNISTACK_MODULE_MODULE_HPP

#include <iostream>
#include <cstdint>
#include <functional>
#include <memory>
#include <concepts>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <omnistack/packet/packet.hpp>
#include <omnistack/module/event.hpp>
#include <omnistack/common/constant.hpp>

namespace omnistack::data_plane {

    using namespace omnistack::packet;
    
    class BaseModule {
    public:
        typedef std::function<bool(Packet* packet)> Filter;

        enum class ModuleType {
            kReadOnly,
            kReadWrite,
            kOccupy
        };

        enum class FilterGroupType {
            kMutex,
            kEqual
        };

        BaseModule() {};
        virtual ~BaseModule() {};

        static constexpr bool DefaultFilter(Packet* packet){ return true; }

        void RegisterDownstreamFilters(const std::vector<std::pair<uint32_t, uint32_t>>& modules, const std::vector<Filter>& filters, const std::vector<uint32_t>& filter_masks, const std::vector<std::set<uint32_t>>& groups, const std::vector<FilterGroupType>& group_types);

        void set_raise_event_(std::function<void(Event* event)> raise_event);

        void set_upstream_nodes_(const std::vector<std::pair<uint32_t, uint32_t>>& upstream_nodes);

        void ApplyDownstreamFilters(Packet* packet);

        /**
         * @brief Get the Filter object for certain upstream node
         * @param upstream_node upstream node identifier (Crc32 of name)
         * @param global_id global identifier in graph of this module
         * @return Filter for certain link
         * @note This function will be called before Initialize(), and will be called only once for each upstream node
        */
        virtual Filter GetFilter(uint32_t upstream_node, uint32_t global_id) { return DefaultFilter; }

        virtual Packet* MainLogic(Packet* packet) { return packet; }

        virtual Packet* TimerLogic(uint64_t tick) { return nullptr; }

        /**
         * @brief Initialize module
         * @param name_prefix name prefix of this module, used to avoid name conflict
         * @param packet_pool packet pool for this module
         * @note All protected members will be initialized before this function called
        */
        virtual void Initialize(std::string_view name_prefix, PacketPool* packet_pool) {};

        virtual void Destroy() {};

        virtual Packet* EventCallback(Event* event) { return nullptr; }

        virtual std::vector<Event::EventType> RegisterEvents() { return {}; }

        virtual constexpr bool allow_duplication_() { return false; }

        virtual constexpr uint32_t name_() { return common::ConstCrc32("BaseModule"); }

        virtual constexpr ModuleType type_() { return ModuleType::kOccupy; }

        /* when does this act? will it be done in son-class? */
        // uint32_t burst_ = 1;

    protected:
        struct FilterGroup {
            std::vector<Filter> filters;
            std::vector<uint32_t> filter_masks;
            uint32_t universe_mask;
            FilterGroupType type;
            uint8_t last_apply;
        };

        struct DownstreamInfo {
            uint32_t module_type;
            uint32_t module_id;
            uint32_t filter_mask;
        };

        /* event must be processed immediately while raising */
        std::function<void(Event* event)> raise_event_;
        std::vector<FilterGroup> filter_groups_;
        std::vector<std::pair<uint32_t, uint32_t>> upstream_nodes_;
        std::vector<DownstreamInfo> downstream_nodes_;
    };

    class ModuleFactory {
    public:
        typedef std::function<std::unique_ptr<BaseModule>()> CreateFunction;

        static ModuleFactory& instance_() {
            static ModuleFactory factory;
            return factory;
        }

        void Register(uint32_t name, const CreateFunction& func) {
            if(module_list_.find(name) != module_list_.end()) {
                /* TODO: report error */
                std::cerr << "module name conflict: " << name << "\n";
                return;
            }
            if(func == nullptr) {
                /* TODO: report error */
                return;
            }
            if(!module_list_.insert(std::make_pair(name, func)).second) {
                /* TODO: report error */
                return;
            }
        }

        [[nodiscard]] std::unique_ptr<BaseModule> Create(uint32_t name) const {
            auto it = module_list_.find(name);
            if(it == module_list_.end()) {
                /* TODO: report error */
                return nullptr;
            }
            return it->second();
        }

    private:
        std::map<uint32_t, CreateFunction> module_list_;
    };

    template<typename T, const char name[]>
    class Module : public BaseModule {
    public:
        static std::unique_ptr<BaseModule> CreateModuleObject() {
            return std::make_unique<T>();
        }

        constexpr uint32_t name_() override { return common::ConstCrc32(name); }

        struct FactoryEntry {
            FactoryEntry() {
                ModuleFactory::instance_().Register(common::ConstCrc32(name), CreateModuleObject);
            }
            inline void DoNothing() const {}
        };

        static const FactoryEntry factory_entry_;

        Module() {
            factory_entry_.DoNothing();
        }
        virtual ~Module() {
            factory_entry_.DoNothing();
        }
    };

    template<typename T, const char name[]>
    const typename Module<T, name>::FactoryEntry Module<T, name>::factory_entry_;
}

#endif //OMNISTACK_MODULE_MODULE_HPP
