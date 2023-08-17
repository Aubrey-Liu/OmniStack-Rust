//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_MODULE_HPP
#define OMNISTACK_MODULE_MODULE_HPP

#include <iostream>
#include <cstdint>
#include <functional>
#include <memory>
// #include <concepts>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <omnistack/packet/packet.hpp>
#include <omnistack/module/event.hpp>

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

        void RegisterDownstreamFilters(const std::vector<Filter>& filters, const std::vector<uint32_t>& filter_masks, const std::vector<std::set<uint32_t>>& groups, const std::vector<FilterGroupType>& group_types);

        void set_raise_event_(std::function<void(Event* event)> raise_event);

        void set_upstream_nodes_(const std::vector<std::pair<std::string, uint32_t>>& upstream_nodes);

        void ApplyDownstreamFilters(Packet* packet);

        virtual Filter GetFilter(std::string_view upstream_module, uint32_t global_id) { return DefaultFilter; }

        virtual Packet* MainLogic(Packet* packet) { return packet; }

        virtual Packet* TimerLogic(uint64_t tick) { return nullptr; }

        virtual void Initialize(std::string_view name_prefix, PacketPool* packet_pool) {};

        virtual void Destroy() {};

        virtual Packet* EventCallback(Event* event) { return nullptr; }

        virtual std::vector<Event::EventType> RegisterEvents() { return {}; }

        virtual constexpr bool allow_duplication_() { return false; }

        virtual constexpr std::string_view name_() { return "BaseModule"; }

        virtual constexpr ModuleType type_() { return ModuleType::kOccupy; }

        /* when does this act? will it be done in son-class? */
        // uint32_t burst_ = 1;

    protected:
        struct FilterGroup {
            std::vector<Filter> filters_;
            std::vector<uint32_t> filter_masks_;
            uint32_t universe_mask_;
            FilterGroupType type_;
            uint8_t last_apply_;
        };

        /* event must be processed immediately while raising */
        std::function<void(Event* event)> raise_event_;
        std::vector<FilterGroup> filter_groups_;
        /* TODO: change the forward structure to compile-time hash */
        std::vector<std::pair<std::string, uint32_t>> upstream_nodes_;
    };

    class ModuleFactory {
    public:
        typedef std::function<std::unique_ptr<BaseModule>()> CreateFunction;

        static ModuleFactory& instance_() {
            static ModuleFactory factory;
            return factory;
        }

        void Register(const std::string& name, const CreateFunction& func) {
            if(module_list_.find(name) != module_list_.end()) {
                /* TODO: report error */
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
            return std::make_unique<T>();
        }

        constexpr std::string_view name_() override { return std::string_view(name); }

        struct FactoryEntry {
            FactoryEntry() {
                ModuleFactory::instance_().Register(std::string(name), CreateModuleObject);
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
