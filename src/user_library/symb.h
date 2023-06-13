//
// Created by Jeremy Guo on 2023/6/12.
//

#ifndef OMNISTACK_SYMB_H
#define OMNISTACK_SYMB_H

#include <unistd.h>
#include <dlfcn.h>
#include <stdexcept>
#include <pthread.h>

#ifndef omnistack_typeof
#ifndef typeof
#ifdef __clang__
#define omnistack_typeof(x) __typeof__(x)
#else
#error No typeof definition
#endif
#else
#define omnistack_typeof(x) typeof(x)
#endif
#endif

namespace omnistack {
    namespace user_lib {
        namespace posix_api {
            extern omnistack_typeof(::fork)* fork;
            extern omnistack_typeof(::pthread_create)* pthread_create;
        }

        namespace api {
            pid_t fork();

        }
    }
}

#endif //OMNISTACK_SYMB_H
