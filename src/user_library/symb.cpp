#include <unistd.h>

#include "symb.h"

pid_t omnistack::user_lib::api::fork() {
    auto ret = omnistack::user_lib::posix_api::fork();
    /**
     * Establish new connection to control plane
     */
    return ret;
}

extern "C" {
    pid_t fork(void) {
        auto ret = omnistack::user_lib::api::fork();
        return ret;
    }
}