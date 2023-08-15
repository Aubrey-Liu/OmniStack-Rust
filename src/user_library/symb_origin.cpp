//
// Created by Jeremy Guo on 2023/6/12.
//

#include "symb.h"

namespace omnistack {
    namespace user_lib {
        namespace posix_api {
            omnistack_typeof(::fork)* fork = nullptr;
            omnistack_typeof(::pthread_create)* pthread_create = nullptr;
            omnistack_typeof(::socket)* socket = nullptr;
            omnistack_typeof(::close)* close = nullptr;

            omnistack_typeof(::read)* read = nullptr;
            omnistack_typeof(::recv)* recv = nullptr;
            omnistack_typeof(::recvfrom)* recvfrom = nullptr;

            omnistack_typeof(::write)* write = nullptr;
            omnistack_typeof(::send)* send = nullptr;
            omnistack_typeof(::sendto)* sendto = nullptr;

#if defined (__APPLE__)
            omnistack_typeof(::kqueue)* kqueue = nullptr;
            omnistack_typeof(::kevent)* kevent = nullptr;
            omnistack_typeof(::kevent64)* kevent64 = nullptr;
#elif defined(__linux__)
            omnistack_typeof(::epoll_create)* epoll_create = nullptr;
            omnistack_typeof(::epoll_wait)* epoll_wait = nullptr;
            omnistack_typeof(::epoll_ctl)* epoll_ctl = nullptr;
#endif

            void InitAPI() {
#define INIT_API(api) \
                do {      \
                    posix_api::api = (omnistack_typeof(::api)*) dlsym(RTLD_NEXT, #api); \
                    if (!posix_api::api) \
                        throw std::runtime_error(#api " not found in RTLD_NEXT");       \
                } while(0)

                INIT_API(fork);
                INIT_API(pthread_create);
                INIT_API(socket);
                INIT_API(close);

                INIT_API(read);
                INIT_API(recv);
                INIT_API(recvfrom);

                INIT_API(write);
                INIT_API(send);
                INIT_API(sendto);

#if defined (__APPLE__)
                INIT_API(kqueue);
                INIT_API(kevent);
                INIT_API(kevent64);
#elif defined(__linux__)
                INIT_API(epoll_create);
                INIT_API(epoll_wait);
                INIT_API(epoll_ctl);
#endif

#undef INIT_API
            }
        }
    }
}