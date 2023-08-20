#ifndef OMNISTACK_HASHTABLE_HPP
#define OMNISTACK_HASHTABLE_HPP

#include <functional>
#include <cstdint>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_hash.h>
#include <rte_hash_crc.h>
#else
#include <emmintrin.h>
#include <smmintrin.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <map>
#endif

/** WARNING HASHTABLE CAN ONLY BE USED LOCALLY **/
namespace omnistack::hashtable {
    constexpr uint32_t kDefaultHashtableSize = 1024;

#if defined(OMNIMEM_BACKEND_DPDK)
    inline namespace dpdk {
        class Hashtable {
        public:
            typedef uint32_t HashValue;
            typedef std::function<void(const void* key, void* value, void* param)> ForeachCallback;

            static Hashtable* Create(uint32_t max_entries, uint32_t key_len);
        
            static void Destroy(Hashtable* hashtable);
        
            int32_t Insert(const void* key, void* value, HashValue hash_value);

            int32_t InsertKey(const void* key, HashValue hash_value);

            int32_t Delete(const void* key, HashValue hash_value);

            void* Lookup(const void* key, HashValue hash_value);

            bool LookupKey(const void* key, HashValue hash_value);

            void Foreach(ForeachCallback callback, void* param);

        private:
            Hashtable() = default;
            Hashtable(Hashtable&&) = default;
            Hashtable(const Hashtable&) = default;
            ~Hashtable() = default;

            rte_hash* hash_table_;
        };

        extern pthread_once_t init_once_flag;
        extern pthread_spinlock_t create_spinlock;
        void InitOnce() {
            pthread_spin_init(&create_spinlock, PTHREAD_PROCESS_PRIVATE);
        }

        inline Hashtable* Hashtable::Create(uint32_t max_entries, uint32_t key_len) {
            auto hashtable = new Hashtable();
            pthread_once(&init_once_flag, InitOnce);
            pthread_spin_lock(&create_spinlock);
            rte_hash_parameters params{};
            if(max_entries == 0) max_entries = kDefaultHashtableSize;
            params.entries = max_entries;
            params.key_len = key_len;
            params.hash_func = nullptr;
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

        inline int32_t Hashtable::Insert(const void* key, void* value, HashValue hash_value) {
            return rte_hash_add_key_with_hash_data(hash_table_, key, hash_value, value);
        }

        inline int32_t Hashtable::Delete(const void* key, HashValue hash_value) {
            return rte_hash_del_key_with_hash(hash_table_, key, hash_value);
        }

        inline void* Hashtable::Lookup(const void* key, HashValue hash_value) {
            void* value;
            if(rte_hash_lookup_with_hash_data(hash_table_, key, hash_value, &value) < 0) [[unlikely]]
                return nullptr;
            return value;
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
#else
    inline namespace _linux {
        static inline int
        HashK16CmpEq(const void *key1, const void *key2, size_t len) {
            const __m128i k1 = _mm_loadu_si128((const __m128i *) key1);
            const __m128i k2 = _mm_loadu_si128((const __m128i *) key2);
            const __m128i x = _mm_xor_si128(k1, k2);

            return !_mm_test_all_zeros(x, x);
        }

        static inline int
        HashK32CmpEq(const void *key1, const void *key2, size_t len) {
            return HashK16CmpEq(key1, key2, len) ||
                HashK16CmpEq((const char *) key1 + 16,
                        (const char *) key2 + 16, len);
        }

        static inline int
        HashK48CmpEq(const void *key1, const void *key2, size_t len) {
            return HashK16CmpEq(key1, key2, len) ||
                HashK16CmpEq((const char *) key1 + 16,
                        (const char *) key2 + 16, len) ||
                HashK16CmpEq((const char *) key1 + 32,
                        (const char *) key2 + 32, len);
        }

        static inline int
        HashK64CmpEq(const void *key1, const void *key2, size_t len)
        {
            return HashK32CmpEq(key1, key2, len) ||
                HashK32CmpEq((const char *) key1 + 32,
                        (const char *) key2 + 32, len);
        }

        typedef std::function<int(const void* key1, const void* key2, size_t len)> HashCompareFunc;
        static HashCompareFunc funcs[] = {
            HashK16CmpEq,
            HashK32CmpEq,
            HashK48CmpEq,
            HashK64CmpEq,
            ::memcmp
        };

        static inline uint32_t
        hash_crc(const void *data, uint32_t data_len, uint32_t init_val) {
            unsigned i;
            uintptr_t pd = (uintptr_t) data;
            for (i = 0; i < data_len / 8; i++) {
                init_val = hash_crc_8byte(*(const uint64_t *)pd, init_val);
                pd += 8;
            }
            if (data_len & 0x4) {
                init_val = hash_crc_4byte(*(const uint32_t *)pd, init_val);
                pd += 4;
            }
            if (data_len & 0x2) {
                init_val = hash_crc_2byte(*(const uint16_t *)pd, init_val);
                pd += 2;
            }
            if (data_len & 0x1)
                init_val = hash_crc_1byte(*(const uint8_t *)pd, init_val);
            return init_val;
        }

        class Hashtable {
        public:
            typedef uint32_t HashValue;
            typedef std::function<void(const void* key, void* value, void* param)> ForeachCallback;

            inline static Hashtable* Create(uint32_t max_entries, uint32_t key_len) {
                auto ret = new Hashtable();
                switch (key_len) {
                    case 16: ret->cmp_func_ = funcs[0]; break;
                    case 32: ret->cmp_func_ = funcs[1]; break;
                    case 48: ret->cmp_func_ = funcs[2]; break;
                    case 64: ret->cmp_func_ = funcs[3]; break;
                    default: ret->cmp_func_ = funcs[4]; break;
                }
                ret->key_len_ = key_len;
                return ret;
            }
        
            inline static void Destroy(Hashtable* hashtable) {
                delete hashtable;
            }
        
            inline int32_t Insert(const void* key, void* value, HashValue hash_value) {
                std::lock_guard<std::mutex> lock(lock_);
                auto new_key = malloc(key_len_);
                memcpy(new_key, key, key_len_);
                map_.insert(std::make_pair(hash_value, std::make_pair(new_key, value)));
                return 0;
            }

            int32_t Delete(const void* key, HashValue hash_value) {
                std::lock_guard<std::mutex> lock(lock_);
                auto range = map_.equal_range(hash_value);
                for(auto it = range.first; it != range.second; ++it) {
                    if(cmp_func_(it->second.first, key, key_len_) == 0) {
                        if (it->second.second != it->second.first) [[unlikely]]
                            free(const_cast<void*>(it->second.first));
                        map_.erase(it);
                        return 0;
                    }
                }
                return -1;
            }

            void* Lookup(const void* key, HashValue hash_value) {
                std::lock_guard<std::mutex> lock(lock_);
                auto range = map_.equal_range(hash_value);
                for(auto it = range.first; it != range.second; ++it) {
                    if(cmp_func_(it->second.first, key, key_len_) == 0) {
                        return it->second.second;
                    }
                }
                return nullptr;
            }

            void* Lookup(const void* key) {
                std::lock_guard<std::mutex> lock(lock_);
                HashValue hash_value = hash_crc(key, key_len_);
                auto range = map_.equal_range(hash_value);
                for(auto it = range.first; it != range.second; ++it) {
                    if(cmp_func_(it->second.first, key, key_len_) == 0) {
                        return it->second.second;
                    }
                }
                return nullptr; 
            }

            void Foreach(ForeachCallback callback, void* param) {
                std::lock_guard<std::mutex> lock(lock_);
                for(auto& it : map_) {
                    callback(it.second.first, it.second.second, param);
                }
            }

        private:
            std::mutex lock_;
            std::multimap<uint32_t, std::pair<const void*, void*>> map_;
            HashCompareFunc cmp_func_;
            uint32_t key_len_;

            Hashtable() = default;
            Hashtable(Hashtable&&) = default;
            Hashtable(const Hashtable&) = default;
            ~Hashtable() = default;
        };
    }
#endif

}

#endif //OMNISTACK_HASHTABLE_HPP