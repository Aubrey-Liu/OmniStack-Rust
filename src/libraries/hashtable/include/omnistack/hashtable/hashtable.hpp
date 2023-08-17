//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_HASHTABLE_HPP
#define OMNISTACK_HASHTABLE_HPP

#include <functional>
#include <cstdint>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_hash.h>
#include <rte_hash_crc.h>
#endif

namespace omnistack::hashtable {

    constexpr uint32_t kDefaultHashtableSize = 1024;

#if defined(OMNIMEM_BACKEND_DPDK)
    inline namespace dpdk {
        class Hashtable {
        public:
            typedef uint32_t HashValue;
            typedef std::function<void(const void* key, void* value, void* param)> ForeachCallback;

            static Hashtable* Create(uint32_t max_entries, uint32_t key_len, rte_hash_function hash_func = rte_hash_crc);
        
            static void Destroy(Hashtable* hashtable);

            int32_t Insert(const void* key, void* value);
        
            int32_t Insert(const void* key, void* value, HashValue hash_value);

            int32_t InsertKey(const void* key);

            int32_t InsertKey(const void* key, HashValue hash_value);

            int32_t Delete(const void* key);

            int32_t Delete(const void* key, HashValue hash_value);

            void* Lookup(const void* key);

            void* Lookup(const void* key, HashValue hash_value);

            bool LookupKey(const void* key);

            bool LookupKey(const void* key, HashValue hash_value);

            void Foreach(ForeachCallback callback, void* param);

        private:
            Hashtable() = default;
            Hashtable(Hashtable&&) = default;
            Hashtable(const Hashtable&) = default;
            ~Hashtable() = default;

            rte_hash* hash_table_;
        };

        static pthread_once_t once_control = PTHREAD_ONCE_INIT;
        static pthread_spinlock_t create_spinlock;
        static void init_once() {
            pthread_spin_init(&create_spinlock, PTHREAD_PROCESS_PRIVATE);
        }

        inline Hashtable* Hashtable::Create(uint32_t max_entries, uint32_t key_len, rte_hash_function hash_func) {
            auto hashtable = new Hashtable();
            pthread_once(&once_control, init_once);
            pthread_spin_lock(&create_spinlock);
            rte_hash_parameters params{};
            if(max_entries == 0) max_entries = kDefaultHashtableSize;
            params.entries = max_entries;
            params.key_len = key_len;
            params.hash_func = hash_func;
            params.socket_id = rte_socket_id();
            static int name_id = 0;
            char name[32];
            sprintf(name, "hashtable_%d", name_id++);
            params.name = name;
            hashtable->hash_table_ = rte_hash_create(&params);
            pthread_spin_unlock(&create_spinlock);
            return hashtable;
        }

        inline void Hashtable::Destroy(Hashtable* hashtable) {
            rte_hash_free(hashtable->hash_table_);
            delete hashtable;
        }

        inline int32_t Hashtable::Insert(const void* key, void* value) {
            return rte_hash_add_key_data(hash_table_, key, value);
        }

        inline int32_t Hashtable::Insert(const void* key, void* value, HashValue hash_value) {
            return rte_hash_add_key_with_hash_data(hash_table_, key, hash_value, value);
        }

        inline int32_t Hashtable::InsertKey(const void* key) {
            return rte_hash_add_key(hash_table_, key);
        }

        inline int32_t Hashtable::InsertKey(const void* key, HashValue hash_value) {
            return rte_hash_add_key_with_hash(hash_table_, key, hash_value);
        }

        inline int32_t Hashtable::Delete(const void* key) {
            return rte_hash_del_key(hash_table_, key);
        }

        inline int32_t Hashtable::Delete(const void* key, HashValue hash_value) {
            return rte_hash_del_key_with_hash(hash_table_, key, hash_value);
        }

        inline void* Hashtable::Lookup(const void* key) {
            void* value;
            if(rte_hash_lookup_data(hash_table_, key, &value) < 0) return nullptr;
            return value;
        }

        inline void* Hashtable::Lookup(const void* key, HashValue hash_value) {
            void* value;
            if(rte_hash_lookup_with_hash_data(hash_table_, key, hash_value, &value) < 0) return nullptr;
            return value;
        }

        inline bool Hashtable::LookupKey(const void* key) {
            return rte_hash_lookup(hash_table_, key) >= 0;
        }

        inline bool Hashtable::LookupKey(const void* key, HashValue hash_value) {
            return rte_hash_lookup_with_hash(hash_table_, key, hash_value) >= 0;
        }

        inline void Hashtable::Foreach(ForeachCallback callback, void* param) {
            const void* key;
            void* value;
            uint32_t iter = 0;
            while(rte_hash_iterate(hash_table_, &key, &value, &iter) >= 0) {
                callback(key, value, param);
            }
        }
    }
#endif

}

#endif //OMNISTACK_HASHTABLE_HPP