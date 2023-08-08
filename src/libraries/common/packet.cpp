//
// Created by Jeremy Guo on 2023/6/9.
//

#include <omnistack/common/packet.hpp>

namespace omnistack::common {
    
}

namespace memory {

#ifdef OMNIMEM_BACKEND_LINUX
    inline namespace linux {
        void InitializeSubsystem(int control_plane_id = 0);
    }
#endif

#ifdef OMNIMEM_BACKEND_DPDK
    inline namespace dpdk {
        void InitializeSubsystem(int control_plane_id = 0, bool init_dpdk = false);
    }
#endif

}