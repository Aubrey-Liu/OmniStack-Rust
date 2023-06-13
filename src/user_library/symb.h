//
// Created by Jeremy Guo on 2023/6/12.
//

#ifndef OMNISTACK_SYMB_H
#define OMNISTACK_SYMB_H

#include <unistd.h>
#include <dlfcn.h>
#include <stdexcept>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#if defined(__APPLE__)
#include <sys/event.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#endif

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
            extern omnistack_typeof(::socket)* socket;
            extern omnistack_typeof(::close)* close;

            extern omnistack_typeof(::read)* read;
            extern omnistack_typeof(::recv)* recv;
            extern omnistack_typeof(::recvfrom)* recvfrom;

            extern omnistack_typeof(::write)* write;
            extern omnistack_typeof(::send)* send;
            extern omnistack_typeof(::sendto)* sendto;

#if defined(__APPLE__)
            extern omnistack_typeof(::kqueue)* kqueue;
            extern omnistack_typeof(::kevent)* kevent;
            extern omnistack_typeof(::kevent64)* kevent64;
#elif defined(__linux__)
            extern omnistack_typeof(::epoll_create)* epoll_create;
            extern omnistack_typeof(::epoll_wait)* epoll_wait;
            extern omnistack_typeof(::epoll_ctl)* epoll_ctl;
#endif
        }

        namespace api {
            omnistack_typeof(::fork) fork;
            omnistack_typeof(::pthread_create) pthread_create;
            omnistack_typeof(::socket) socket;
            omnistack_typeof(::close) close;
        }
    }
}

#endif //OMNISTACK_SYMB_H
