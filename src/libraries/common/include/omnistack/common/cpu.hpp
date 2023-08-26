//
// Created by liuhao on 23-8-26.
//

#ifndef OMNISTACK_COMMON_CPU_HPP
#define OMNISTACK_COMMON_CPU_HPP

#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <thread>

namespace omnistack::common {

    inline int GetNumCpus() {
        return std::thread::hardware_concurrency();
    }

    inline pid_t GetTid() {
        return syscall(SYS_gettid);
    }

    inline int CoreAffinitize(int core_id) {
        int num_cpus = GetNumCpus();
        if(core_id < 0 || core_id >= num_cpus) return -1;
        
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(core_id, &mask);
        auto ret = sched_setaffinity(GetTid(), sizeof(mask), &mask);
    
        /* TODO: set DPDK if required */

        /* TODO: set NUMA */

        return ret;
    }

}

#endif // OMNISTACK_COMMON_CPU_HPP