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
#include <omnistack/common/logger.h>

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

        void set_raise_event_(std::function<void(Event* event)> raise_event) { raise_event_ = raise_event; }

        void set_upstream_nodes_(const std::vector<std::pair<uint32_t, uint32_t>>& upstream_nodes) { upstream_nodes_ = upstream_nodes; }

        void ApplyDownstreamFilters(Packet* packet);

        /**
         * @brief Get the Filter object for certain upstream node
         * @param upstream_node upstream node identifier (Crc32 of name)
         * @param global_id global identifier in graph of this module
         * @return Filter for certain link
         * @note This function will be called before Initialize(), and will be called only once for each upstream node
        */
        virtual Filter GetFilter(uint32_t upstream_node, uint32_t global_id) { return DefaultFilter; }

        /** For MainLogic, TimerLogic, EventCallback,
         *  if multiple packages are returned, 
         *  it is recommended to return them in reverse order. 
        */

        virtual Packet* MainLogic(Packet* packet) { return packet; }

        virtual Packet* TimerLogic(uint64_t tick) { return nullptr; }

        virtual Packet* EventCallback(Event* event) { return nullptr; }

        virtual std::vector<Event::EventType> RegisterEvents() { return {}; }

        /**
         * @brief Initialize module
         * @param name_prefix name prefix of this module, used to avoid name conflict
         * @param packet_pool packet pool for this module
         * @note All protected members will be initialized before this function called
        */
        virtual void Initialize(std::string_view name_prefix, PacketPool* packet_pool) {};

        virtual void Destroy() {};

        virtual constexpr bool allow_duplication_() { return false; }

        virtual constexpr uint32_t name_() { return common::ConstCrc32("BaseModule"); }

        virtual constexpr std::string_view name_str_() { return "BaseModule"; }

        virtual constexpr ModuleType type_() { return ModuleType::kOccupy; }

        virtual constexpr bool has_timer_() { return false; }

    protected:
        struct FilterGroup {
            /* indexed by index in downstream_filters_ */
            std::vector<uint32_t> filter_masks_true;
            std::vector<uint32_t> filter_masks_false;
            std::vector<uint32_t> filter_idx;   /* index in downstream_filters_ */
            uint32_t universe_mask;
            FilterGroupType type;
        };

        struct DownstreamInfo {
            uint32_t module_type;
            uint32_t module_id;
            uint32_t filter_mask;
        };

        /** Event will be processed immediately while raising.
         *  However, the packet returned from the callback will be add to packet queue and processed later.
        */
        std::function<void(Event* event)> raise_event_;
        std::vector<FilterGroup> filter_groups_;
        std::vector<Filter> downstream_filters_;
        std::vector<std::pair<uint32_t, uint32_t>> upstream_nodes_;
        std::vector<DownstreamInfo> downstream_nodes_;
    };

    inline void BaseModule::ApplyDownstreamFilters(Packet *packet) {
        auto& mask = packet->next_hop_filter_;
        for(auto& group : filter_groups_) {
            auto cantidate = mask & group.universe_mask;
            if(!cantidate) [[unlikely]] continue;
            if(group.type == FilterGroupType::kMutex) [[likely]] {
                do {
                    auto idx = std::countr_zero(cantidate);
                    if(downstream_filters_[idx](packet)) [[likely]] {
                        mask &= group.filter_masks_true[idx];
                        cantidate &= group.filter_masks_true[idx];
                        break;
                    }
                    else {
                        mask &= group.filter_masks_false[idx];
                        cantidate &= group.filter_masks_false[idx];
                    }
                } while(cantidate);
            }
            else {
                auto idx = packet->flow_hash_ % group.filter_idx.size();
                idx = group.filter_idx[idx];
                if(downstream_filters_[idx](packet)) [[likely]] mask &= group.filter_masks_true[idx];
                else mask &= group.filter_masks_false[idx];
            }
        }
    }

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
                std::cerr << "module not found: " << name << "\n";
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

        constexpr std::string_view name_str_() override { return name; }

        struct FactoryEntry {
            FactoryEntry() {
                ModuleFactory::instance_().Register(common::ConstCrc32(name), CreateModuleObject);
            }
            inline void DoNothing() const {}
        };

        inline static const FactoryEntry factory_entry_;

        Module() {
            factory_entry_.DoNothing();
        }
        virtual ~Module() {
            factory_entry_.DoNothing();
        }
    };

    /* not need in C++17 */
    // template<typename T, const char name[]>
    // inline const typename Module<T, name>::FactoryEntry Module<T, name>::factory_entry_;

}

#endif //OMNISTACK_MODULE_MODULE_HPP
