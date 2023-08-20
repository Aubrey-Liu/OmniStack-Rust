#include <omnistack/hashtable/hashtable.h>

namespace omnistack::hashtable {
#if defined(OMNIMEM_BACKEND_DPDK)
    inline namespace dpdk {
        pthread_once_t init_once_flag = PTHREAD_ONCE_INIT;
        pthread_spinlock_t create_spinlock;

        void InitOnce() {
            pthread_spin_init(&create_spinlock, PTHREAD_PROCESS_PRIVATE);
        }
    }
#endif
}