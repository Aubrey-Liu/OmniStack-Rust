#include <unistd.h>

#include "symb.h"

pid_t omnistack::user_lib::api::fork()
#ifdef __linux__
noexcept
#endif
{
    auto ret = omnistack::user_lib::posix_api::fork();
    /**
     * Establish new connection to control plane
     */
    return ret;
}

#if defined(OMNIAPI_DYNAMIC)
extern "C" {
    pid_t fork(void) {
        auto ret = omnistack::user_lib::api::fork();
        return ret;
    }
}
#endif