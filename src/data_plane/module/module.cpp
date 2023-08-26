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

}