//
// Created by liuhao on 23-6-10.
//

#include <omnistack/module/module.hpp>

namespace omnistack::data_plane {
    BaseModule::BaseModule() {}

    void
    BaseModule::RegisterDownstreamFilters(const std::vector<Filter> &filters, const std::vector<uint32_t> &filter_masks,
                                      const std::vector<uint32_t> &group_ids,
                                      const std::vector<FilterGroupType> &group_types) {

    }

    void BaseModule::ApplyDownstreamFilters(omnistack::data_plane::DataPlanePacket &packet) {

    }

}