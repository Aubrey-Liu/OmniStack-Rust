//
// Created by liuhao on 23-6-10.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane {
    
    void BaseModule::RegisterDownstreamFilters(const std::vector<std::pair<uint32_t, uint32_t>>& modules,
                                               const std::vector<Filter> &filters,
                                               const std::vector<uint32_t> &filter_masks,
                                               const std::vector<std::set<uint32_t>> &groups,
                                               const std::vector<FilterGroupType> &group_types) {
        downstream_nodes_.resize(modules.size());
        downstream_filters_.resize(modules.size());
        for(int i = 0; i < modules.size(); i ++) {
            auto [type, id] = modules[i];
            downstream_nodes_[i].module_type = type;
            downstream_nodes_[i].module_id = id;
            downstream_nodes_[i].filter_mask = filter_masks[i];
            downstream_filters_[i] = filters[i];
        }

        std::vector<uint32_t> filter_equal_masks(filters.size());
        for(int i = 0; i < filters.size(); i ++) {
            filter_equal_masks[i] = filter_masks[i];
            for(int i = 0; i < groups.size(); i ++) {
                auto& group = groups[i];
                auto group_type = group_types[i];
                if(group_type == FilterGroupType::kMutex) continue;
                for(auto idx : group) filter_equal_masks[i] |= filter_masks[idx];
            }
        }

        filter_groups_.resize(groups.size());
        for(int i = 0; i < groups.size(); i ++) {
            auto& group_ids = groups[i];
            auto& group = filter_groups_[i];

            group.type = group_types[i];
            group.filter_idx.resize(0);
            for(auto idx : group_ids) group.filter_idx.push_back(idx);

            group.universe_mask = 0;
            group.filter_masks_false.resize(filters.size(), ~0);
            group.filter_masks_true.resize(filters.size(), ~0);
            if(group_types[i] == FilterGroupType::kMutex) {
                for(auto idx : group_ids) {
                    group.filter_masks_false[idx] = filter_equal_masks[idx];
                    group.universe_mask |= filter_equal_masks[idx];
                }
                for(auto idx : group_ids) {
                    group.filter_masks_true[idx] = ~group.universe_mask | group.filter_masks_false[idx];
                    group.filter_masks_false[idx] = ~group.filter_masks_false[idx];
                }
            }
            else {
                for(auto idx : group_ids) {
                    group.filter_masks_false[idx] = filter_masks[idx];
                    group.universe_mask |= filter_masks[idx];
                }
                for(auto idx : group_ids) {
                    group.filter_masks_true[idx] = ~group.universe_mask | group.filter_masks_false[idx];
                    group.filter_masks_false[idx] = ~group.universe_mask;
                }
            }
        }

        /* if a filter is not in any filter group, add it as an independent group */
        std::vector<bool> in_group(filters.size(), false);
        for(int i = 0; i < groups.size(); i ++)
            for(auto idx : groups[i])
                in_group[idx] = true;
    
        for(int i = 0; i < filters.size(); i ++)
            if(!in_group[i]) {
                filter_groups_.emplace_back();
                auto& group = filter_groups_.back();
                group.universe_mask = filter_masks[i];
                group.type = FilterGroupType::kMutex;
                group.filter_masks_false.resize(filters.size(), ~0);
                group.filter_masks_true.resize(filters.size(), ~0);
                group.filter_masks_false[i] = ~filter_masks[i];
            }
    }

}