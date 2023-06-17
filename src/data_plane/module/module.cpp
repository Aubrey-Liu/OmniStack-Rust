//
// Created by liuhao on 23-6-10.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane {
    BaseModule::BaseModule() {}

    void
    BaseModule::RegisterDownstreamFilters(const std::vector<Filter> &filters, const std::vector<uint32_t> &filter_masks,
                                      const std::vector<std::set<uint32_t>> &groups,
                                      const std::vector<FilterGroupType> &group_types) {

    }

    void BaseModule::set_upstream_nodes(const std::vector<std::string> &upstream_nodes) {}

    void BaseModule::ApplyDownstreamFilters(omnistack::data_plane::DataPlanePacket &packet) {

    }

}