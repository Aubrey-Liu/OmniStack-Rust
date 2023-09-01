//
// Created by liuhao on 23-7-31.
//

#ifndef OMNISTACK_COMMON_CONSTANT_HPP
#define OMNISTACK_COMMON_CONSTANT_HPP

#include <cstdint>

namespace omnistack::common {

    constexpr uint32_t kCacheLineSize = 64;
    constexpr uint32_t kMtu = 1500;
    
    constexpr uint32_t kMaxThread = 1024;

    constexpr uint32_t kDefaultPacketPoolSize = 8192;
}

#endif //OMNISTACK_COMMON_CONSTANT_HPP
