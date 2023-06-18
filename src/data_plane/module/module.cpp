//
// Created by liuhao on 23-6-10.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane {
    void
    BaseModule::RegisterDownstreamFilters(const std::vector<Filter> &filters, const std::vector<uint32_t> &filter_masks,
                                      const std::vector<std::set<uint32_t>> &groups,
                                      const std::vector<FilterGroupType> &group_types) {
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
                group.filter_masks[i] ^= group.universe_mask;
        }
    }

    void BaseModule::set_upstream_nodes(const std::vector<std::string> &upstream_nodes) {
        upstream_nodes_ = upstream_nodes;
    }

    void BaseModule::ApplyDownstreamFilters(omnistack::data_plane::DataPlanePacket *packet) {
        auto& mask = packet->next_hop_filter_;
        for(auto& group : filter_groups_) {
            auto& idx = group.last_apply;
            if(!(mask & group.universe_mask)) [[unlikely]] continue;
            if(group.type == FilterGroupType::kMutex) { [[likely]]
                if(group.filters[idx](packet)) { [[likely]]
                    mask ^= group.filter_masks[idx];
                    continue;
                }
                else {
                    for(int i = 1; i < group.filters.size(); i ++) {
                        idx = (idx + 1) == group.filters.size() ? 0 : (idx + 1);
                        if (group.filters[idx](packet)) {
                            mask ^= group.filter_masks[idx];
                            break;
                        }
                    }
                }
            }
            else {
                idx = (idx + 1) == group.filters.size() ? 0 : (idx + 1);
                mask ^= group.filter_masks[idx];
            }
        }
    }

}