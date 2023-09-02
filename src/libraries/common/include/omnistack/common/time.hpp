//
// Created by liuhao on 23-8-15.
//

#ifndef OMNISTACK_COMMON_TIME_HPP
#define OMNISTACK_COMMON_TIME_HPP

#include <chrono>

namespace omnistack::common {

    inline thread_local uint8_t cnt = 0;
    inline thread_local auto last_time = std::chrono::steady_clock::now();
    
    inline uint64_t NowNs() {
        if(!((++cnt) &= 15)) last_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                last_time.time_since_epoch()).count();
    }

    inline uint64_t NowUs() {
        if(!((++cnt) &= 15)) last_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
                last_time.time_since_epoch()).count();
    }

    inline uint64_t NowMs() {
        if(!((++cnt) &= 15)) last_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                last_time.time_since_epoch()).count();
    }

    inline uint64_t NowMsImm() {
        auto cur_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                cur_time.time_since_epoch()).count();
    }

    inline uint64_t NowS() {
        if(!((++cnt) &= 15)) last_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
                last_time.time_since_epoch()).count();
    }

}

#endif // OMNISTACK_COMMON_TIME_HPP