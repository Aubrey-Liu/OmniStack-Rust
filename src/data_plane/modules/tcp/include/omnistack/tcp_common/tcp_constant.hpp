//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP
#define OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP

#include <cstdint>

namespace omnistack::data_plane::tcp_common {
    constexpr uint32_t kTcpMaxFlowCount = 65536;
    constexpr uint32_t kTcpFlowTableSize = kTcpMaxFlowCount * 2;
}

#endif //OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP
