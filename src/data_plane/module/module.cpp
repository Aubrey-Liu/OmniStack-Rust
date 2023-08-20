//
// Created by liuhao on 23-6-10.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane {
    inline void BaseModule::RegisterDownstreamFilters(const std::vector<std::pair<uint32_t, uint32_t>>& modules,
                                                      const std::vector<Filter> &filters,
                                                      const std::vector<uint32_t> &filter_masks,
                                                      const std::vector<std::set<uint32_t>> &groups,
                                                      const std::vector<FilterGroupType> &group_types) {
        downstream_nodes_.resize(modules.size());
        for(int i = 0; i < modules.size(); i ++) {
            auto& [type, id] = modules[i];
            downstream_nodes_[i].module_type = type;
            downstream_nodes_[i].module_id = id;
            downstream_nodes_[i].filter_mask = filter_masks[i];
        }

        filter_groups_.resize(groups.size());
        for(int i = 0; i < groups.size(); i ++) {
            auto& group_ids = groups[i];
            auto& group = filter_groups_[i];
            group.last_apply = 0;
            group.universe_mask = 0;
            group.type = group_types[i];
            for(auto idx : group_ids) {
                group.filters.push_back(filters[idx]);
                group.filter_masks.push_back(filter_masks[idx]);
                group.universe_mask |= filter_masks[idx];
            }
            for(int j = 0; j < group.filter_masks.size(); j ++)
                group.filter_masks[i] = ~group.universe_mask | group.filter_masks[i];
        }
    }

    inline void BaseModule::set_raise_event_(std::function<void(Event *event)> raise_event) {
        raise_event_ = raise_event;
    }

    inline void BaseModule::set_upstream_nodes_(const std::vector<std::pair<uint32_t, uint32_t>> &upstream_nodes) {
        upstream_nodes_ = upstream_nodes;
    }

    inline void BaseModule::ApplyDownstreamFilters(omnistack::data_plane::Packet *packet) {
        auto& mask = packet->next_hop_filter_;
        for(auto& group : filter_groups_) {
            auto cantidate = mask & group.universe_mask;
            if(!cantidate) [[unlikely]] continue;
            if(group.type == FilterGroupType::kMutex) [[likely]] {
                do {
                    auto idx = std::countr_zero(cantidate);
                    if(group.filters[idx](packet)) [[likely]] {
                        mask &= group.filter_masks[idx];
                        break;
                    }
                    else cantidate ^= (1 << idx);
                } while(cantidate);
                auto idx = std::countr_zero(cantidate);
            }
            else {
                auto idx = packet->flow_hash_ % group.filters.size();
                mask &= group.filter_masks[idx];
            }
        }
    }

}