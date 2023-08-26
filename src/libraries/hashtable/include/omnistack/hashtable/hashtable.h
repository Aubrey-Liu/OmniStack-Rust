#ifndef OMNISTACK_HASHTABLE_HPP
#define OMNISTACK_HASHTABLE_HPP

#include <functional>
#include <cstdint>
#include <string>
#include <map>
#include <mutex>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_hash.h>
#include <rte_hash_crc.h>
#else
#include <emmintrin.h>
#include <smmintrin.h>
#include <algorithm>
#include <cstring>
#include <omnistack/common/hash.hpp>
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
            static Hashtable* Create(const std::string& name, uint32_t max_entries, uint32_t key_len);
        
            static void Destroy(Hashtable* hashtable);
        
            inline int32_t Insert(const void* key, void* value, HashValue hash_value) {
                return rte_hash_add_key_with_hash_data(hash_table_, key, hash_value, value);
            }

            inline int32_t Insert(const void* key, void* value) {
                return rte_hash_add_key_with_hash_data(hash_table_, key, rte_hash_crc(key, key_len_, 0), value);
            }

            inline int32_t Delete(const void* key, HashValue hash_value) {
                return rte_hash_del_key_with_hash(hash_table_, key, hash_value);
            }

            inline int32_t Delete(const void* key) {
                return rte_hash_del_key_with_hash(hash_table_, key, rte_hash_crc(key, key_len_, 0));
            }

            inline void* Lookup(const void* key, HashValue hash_value) {
                void* value;
                if(rte_hash_lookup_with_hash_data(hash_table_, key, hash_value, &value) < 0) [[unlikely]]
                    return nullptr;
                return value;
            }

            inline void* Lookup(const void* key) {
                void* value;
                if(rte_hash_lookup_with_hash_data(hash_table_, key, rte_hash_crc(key, key_len_, 0), &value) < 0) [[unlikely]]
                    return nullptr;
                return value;
            }

            inline void Foreach(ForeachCallback callback, void* param) {
                const void* key;
                void* value;
                uint32_t iter = 0;
                while(rte_hash_iterate(hash_table_, &key, &value, &iter) >= 0) {
                    callback(key, value, param);
                }
            }

        private:
            Hashtable() = default;
            Hashtable(Hashtable&&) = default;
            Hashtable(const Hashtable&) = default;
            ~Hashtable() = default;

            rte_hash* hash_table_;
            uint32_t key_len_;

            static inline std::map<std::string, Hashtable*> hashtable_map_;
            static inline std::mutex create_lock_;
            static inline std::mutex anoy_create_lock_;
        };

        inline Hashtable* Hashtable::Create(const std::string& name, uint32_t max_entries, uint32_t key_len) {
            std::unique_lock<std::mutex> lock(create_lock_);
            if (hashtable_map_.count(name)) {
                auto hashtable = hashtable_map_[name];
                return hashtable;
            }
            auto hashtable = Hashtable::Create(max_entries, key_len);
            hashtable_map_[name] = hashtable;
            return hashtable;
        }

        inline Hashtable* Hashtable::Create(uint32_t max_entries, uint32_t key_len) {
            std::unique_lock<std::mutex> lock(anoy_create_lock_);
            auto hashtable = new Hashtable();
            rte_hash_parameters params{};
            if(max_entries == 0) max_entries = kDefaultHashtableSize;
            params.entries = max_entries;
            params.key_len = key_len;
            hashtable->key_len_ = key_len;
            params.hash_func = nullptr;
            params.extra_flag = RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF
                | RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD;
            params.socket_id = rte_socket_id();
            static int name_id = 0;
            char name[32];
            sprintf(name, "hashtable_%d", name_id++);
            params.name = name;
            hashtable->hash_table_ = rte_hash_create(&params);
            return hashtable;
        }

        inline void Hashtable::Destroy(Hashtable* hashtable) {
            rte_hash_free(hashtable->hash_table_);
            delete hashtable;
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

            static inline std::mutex create_lock_;
            static inline std::map<std::string, Hashtable*> hashtable_map_;
            inline static Hashtable* Create(const std::string& name, uint32_t max_entries, uint32_t key_len) {
                std::unique_lock<std::mutex> lock(create_lock_);
                if (hashtable_map_.count(name)) {
                    auto hashtable = hashtable_map_[name];
                    return hashtable;
                }
                auto hashtable = Create(max_entries, key_len);
                hashtable_map_[name] = hashtable;
                return hashtable;
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

            inline int32_t Insert(const void* key, void* value) {
                HashValue hash_value = omnistack::common::Crc32(static_cast<const char*>(key), key_len_);
                std::lock_guard<std::mutex> lock(lock_);
                auto new_key = malloc(key_len_);
                memcpy(new_key, key, key_len_);
                map_.insert(std::make_pair(hash_value, std::make_pair(new_key, value)));
                return 0;
            }

            inline int32_t Delete(const void* key, HashValue hash_value) {
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

            inline int32_t Delete(const void* key) {
                HashValue hash_value = omnistack::common::Crc32(static_cast<const char*>(key), key_len_);
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

            inline void* Lookup(const void* key, HashValue hash_value) {
                std::lock_guard<std::mutex> lock(lock_);
                auto range = map_.equal_range(hash_value);
                for(auto it = range.first; it != range.second; ++it) {
                    if(cmp_func_(it->second.first, key, key_len_) == 0) {
                        return it->second.second;
                    }
                }
                return nullptr;
            }

            inline void* Lookup(const void* key) {
                HashValue hash_value = omnistack::common::Crc32(static_cast<const char*>(key), key_len_);
                std::lock_guard<std::mutex> lock(lock_);
                auto range = map_.equal_range(hash_value);
                for(auto it = range.first; it != range.second; ++it) {
                    if(cmp_func_(it->second.first, key, key_len_) == 0) {
                        return it->second.second;
                    }
                }
                return nullptr; 
            }

            inline void Foreach(ForeachCallback callback, void* param) {
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