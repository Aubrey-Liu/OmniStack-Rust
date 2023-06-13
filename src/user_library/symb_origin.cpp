//
// Created by Jeremy Guo on 2023/6/12.
//

#include "symb.h"

namespace omnistack {
    namespace user_lib {
        namespace posix_api {
            omnistack_typeof(::fork)* fork = nullptr;
            omnistack_typeof(::pthread_create)* pthread_create = nullptr;

            void InitAPI() {
                posix_api::fork = (omnistack_typeof(::fork)*) dlsym(RTLD_NEXT, "fork");
                if (!posix_api::fork)
                    throw std::runtime_error("fork not found in RTLD_NEXT");

                posix_api::pthread_create = (omnistack_typeof(::pthread_create)*) dlsym(RTLD_NEXT, "pthread_create");
                if (!posix_api::fork)
                    throw std::runtime_error("pthread create not found in RTLD_NEXT");
            }
        }
    }
}