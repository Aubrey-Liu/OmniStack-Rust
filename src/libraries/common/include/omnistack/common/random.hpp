//
// Created by liuhao on 23-8-13.
//

#ifndef OMNISTACK_COMMON_RANDOM_HPP
#define OMNISTACK_COMMON_RANDOM_HPP

#include <random>

namespace omnistack::common {

    inline int Rand32() {
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<uint32_t> distribution(0, UINT32_MAX);
        return distribution(generator);
    }

}

#endif // OMNISTACK_COMMON_RANDOM_HPP