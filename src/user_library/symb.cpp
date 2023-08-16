#include <unistd.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <omnistack/channel/channel.h>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_eal.h>
#endif

#include <omnistack/user_lib/symb.h>

namespace omnistack::user_lib::api {
    pid_t fork()
    #ifdef __linux__
    noexcept
    #endif
    {
        auto ret = omnistack::user_lib::posix_api::fork();
        /**
         * Establish new connection to control plane
         */
        if (ret == -1) return ret;
        if (ret == 0) {
            omnistack::memory::ForkSubsystem();
            omnistack::token::ForkSubsystem();
            omnistack::channel::ForkSubsystem();

            omnistack::memory::InitializeSubsystemThread();
        }
        return ret;
    }
}

namespace omnistack::user_lib {
    void Initialize() {
        omnistack::memory::InitializeSubsystem(0, true);
        omnistack::token::InitializeSubsystem();
        omnistack::channel::InitializeSubsystem();
    }

    void CleanUp() {
        perror("Not implemented");
    }

    void InitializeThread() {
        omnistack::memory::InitializeSubsystemThread();
    }
}

#if defined(OMNIAPI_DYNAMIC)
extern "C" {
    pid_t fork(void) {
        auto ret = omnistack::user_lib::api::fork();
        return ret;
    }
}
#endif