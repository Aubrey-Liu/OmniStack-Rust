//
// Created by liuhao on 23-8-26.
//

#ifndef OMNISTACK_COMMON_THREAD_HPP
#define OMNISTACK_COMMON_THREAD_HPP

#include <pthread.h>

namespace omnistack::common {

    /* use pthread as backend */

    inline int CreateThread(pthread_t* thread, void* (*start_routine)(void*), void* arg) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        auto ret = pthread_create(thread, &attr, start_routine, arg);
    
        /* TODO: set DPDK if required */

        return ret;
    }

    inline int JoinThread(pthread_t thread, void** retval) {
        return pthread_join(thread, retval);
    }

}

#endif // OMNISTACK_COMMON_THREAD_HPP