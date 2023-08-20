#include <omnistack/hashtable/hashtable.h>

namespace omnistack::hashtable {
    pthread_once_t init_once_flag = PTHREAD_ONCE_INIT;
    pthread_spinlock_t create_spinlock;
}