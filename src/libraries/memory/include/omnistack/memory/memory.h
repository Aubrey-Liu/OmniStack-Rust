//
// Created by Jeremy Guo on 2023/6/17.
//

#ifndef OMNISTACK_MEMORY_H
#define OMNISTACK_MEMORY_H

#include <unistd.h>
#include <cstdint>
#include <string>
#include <pthread.h>

namespace omnistack {
    namespace memory {
        constexpr uint64_t kMetaHeadroomSize = 64;
        constexpr uint64_t kMaxTotalAllocateSize = 16ll * 1024 * 1024 * 1024;
        constexpr uint64_t kMaxNameLength = 64;
        constexpr int kMaxProcess = 1024;
        constexpr int kMaxThread = 8192;
        constexpr int kMaxControlPlane = 8;
        constexpr int kMaxIncomingProcess = 16;

        extern uint64_t process_id;
        extern thread_local uint64_t thread_id;

#if defined (OMNIMEM_BACKEND_DPDK)
        static constexpr std::string_view kBackendName = "dpdk";
#else
        static constexpr std::string_view kBackendName = "origin";

#endif
        enum class RegionType {
            kLocal = 0,
            kMempool,
            kNamedShared,
            kMempoolChunk
        };

        class MemoryPool;
        struct RegionMeta {
            RegionType type;
            uint64_t iova;
            uint64_t process_id;
            #if defined (OMNIMEM_BACKEND_DPDK)
            void* addr;
            #else
            uint64_t offset;
            #endif
            size_t size;
            uint64_t ref_cnt;

            union {
#if defined (OMNIMEM_BACKEND_DPDK)
                MemoryPool* mempool;
#else
                uint64_t mempool_offset;
#endif
            };
        };
        static_assert(sizeof(RegionMeta) <= kMetaHeadroomSize);

        /**
         * @brief Initialize the memory subsystem per process
         */
        void InitializeSubsystem(
            int control_plane_id = 0
#if defined (OMNIMEM_BACKEND_DPDK)
            ,bool init_dpdk = false
#endif
        );

        /**
         * @brief Initialize the memory subsystem per thread
         */
        void InitializeSubsystemThread();

        /**
         * @brief Destroy the memory subsystem per thread
         */
        void DestroySubsystemThread();

        /**
         * @brief Destroy the memory subsystem per process
         */
        void DestroySubsystem();

        /**
         * @brief Allocate memory in shared memory by name (Can be used cross process)
         * @param name The name of the memory region
         * @param size
         * @return The pointer if created by other process the same address
         */
        void* AllocateNamedShared(const std::string& name, size_t size);

        /**
         * @brief Allocate memory in shared memory by name (Can be used cross process)
         * @param name The name of the memory region
         * @param size
         * @param thread_id The thread id of the thread that need to use the region
         * @return The pointer if created by other process the same address
         */
        void* AllocateNamedSharedForThread(const std::string& name, size_t size, uint64_t thread_id);

        void FreeNamedShared(void* ptr);

        /**
         * @brief Allocate Memory in Local Memory
         */
        void* AllocateLocal(size_t size);

        /**
         * @brief Free the memory in local memory
         * @param ptr The pointer to the memory region
         */
        void FreeLocal(void* ptr);

        /**
         * @brief  Free the memory allocated with any form (MemoryPool / LocalMemory / SharedMemory)
         * @param ptr the pointer to the region to be free
         */
        void Free(void* ptr);

        /**
         * @brief Start a control plane to monitor all the process / thread that allocate the memory to automatically free memory
         */
        void StartControlPlane(
#if defined (OMNIMEM_BACKEND_DPDK)
            bool init_dpdk = false
#endif
        );
        void StopControlPlane();

        /**
         * @brief Set the owner thread id of the region pointed by ptr
         * @param ptr The pointer to the region
         * @param thread_id The target thread id
         */
#if defined (_MSC_VER_)
        __forceinline
#elif defined(__GNUC__)
        __inline__ __attribute__((always_inline))
#endif
        void SetOwnerProcess(void* ptr, uint64_t process_id) {
            auto meta = (RegionMeta*) (reinterpret_cast<char*>(ptr) - kMetaHeadroomSize);
            meta->process_id = thread_id;
        }

        constexpr uint64_t kMemoryPoolLocalCache = 256;
        struct MemoryPoolBatch {
#if defined (OMNIMEM_BACKEND_DPDK)
            void* addrs[kMemoryPoolLocalCache];        
#else
            uint64_t offsets[kMemoryPoolLocalCache];        
#endif
            uint32_t cnt;
            uint32_t used;
#if defined (OMNIMEM_BACKEND_DPDK)
            MemoryPoolBatch* next;
#else
            uint64_t next;
#endif
            char padding[64 - sizeof(cnt) - sizeof(used) - sizeof(next)];
        };
        static_assert(sizeof(MemoryPoolBatch) % 64 == 0);

        /**
         * @brief This is the base class of memory pool
         *  The implement must require:
         *      1. Each chunk leave kMetaHeadroomSize before each chunk
         *      2. Each Memory Pool Must has the same virtual address in all process
         *      3. Each chunk must has the same virtual address in all process
         */
        class MemoryPool {
        public:
            void* Get();
            static void PutBack(void* ptr);
            void Put(void* ptr);
            void SafePut(void* ptr); // This is used by control plane
            void FlushSafePut(); // This is used by control plane

#if !defined(OMNIMEM_BACKEND_DPDK)
            uint64_t region_offset_; // The offset of payload memory
#else
            uint8_t* region_ptr_;
#endif
            uint64_t chunk_size_; // Chunk Size include metadata header
            uint64_t chunk_count_;

#if !defined(OMNIMEM_BACKEND_DPDK)
            uint64_t batch_block_offset_;
            uint64_t full_block_offset_;
            uint64_t empty_block_offset_;
#else
            uint8_t* batch_block_ptr_;
            MemoryPoolBatch* full_block_ptr_;
            MemoryPoolBatch* empty_block_ptr_;
#endif

            MemoryPoolBatch* recycle_block_;

            MemoryPoolBatch* local_cache_[kMaxThread + 1];
            MemoryPoolBatch* local_free_cache_[kMaxThread + 1];
            pthread_mutex_t recycle_mutex_;
        };

        MemoryPool* AllocateMemoryPool(const std::string& name, size_t chunk_size, size_t chunk_count);

        void FreeMemoryPool(MemoryPool* memory_pool);

        enum class ControlPlaneStatus {
            kStarting = 0,
            kRunning,
            kStopped
        };
        ControlPlaneStatus GetControlPlaneStatus();

        int GetControlPlaneId();

#if !defined(OMNIMEM_BACKEND_DPDK)
        uint8_t** GetVirtBaseAddrs();
#endif

        extern uint8_t** virt_base_addrs;

        template<typename T>
        class Pointer {
        public:
            inline Pointer() {
#if defined (OMNIMEM_BACKEND_DPDK)
                ptr_ = nullptr;
#else
                offset_ = ~0;
#endif
            }

            inline Pointer(T* ptr): 
#if defined (OMNIMEM_BACKEND_DPDK)
                ptr_(ptr)
#else
                offset_(ptr ? ((uint8_t*)ptr - virt_base_addrs[process_id]) : ~0)
#endif
            {}

            inline T* operator->() {
#if defined (OMNIMEM_BACKEND_DPDK)
                return ptr_;
#else
                return (T*)(virt_base_addrs[process_id] + offset_);
#endif
            }

            inline T* operator+(const uint32_t& a) {
#if defined (OMNIMEM_BACKEND_DPDK)
                return ptr_ + a;
#else
                if (offset_ == ~0) [[unlikely]] return (T*)(a * sizeof(T));
                return (T*)(virt_base_addrs[process_id] + offset_ + a * sizeof(T));
#endif
            }

            inline T* operator-(const uint32_t& a) {
#if defined (OMNIMEM_BACKEND_DPDK)
                return ptr_ - a;
#else
                if (offset_ == ~0) [[unlikely]] return (T*)(-a * sizeof(T));
                return (T*)(virt_base_addrs[process_id] + offset_ - a * sizeof(T));
#endif
            }

            template<typename Type> friend inline Type& operator*(const Pointer<Type>& p);

            /**
             * @brief Use this to get the real address of Pointer and then use it in channel
            */
            inline T* Get() const {
#if defined (OMNIMEM_BACKEND_DPDK)
                return ptr_;
#else
                if (offset_ == ~0) [[unlikely]] return nullptr;
                return (T*)(virt_base_addrs[process_id] + offset_);
#endif
            }

            inline void Set(T* ptr) {
#if defined (OMNIMEM_BACKEND_DPDK)
                ptr_ = ptr;
#else
                if (!ptr) [[unlikely]] offset_ = ~0;
                else offset_ = ((uint8_t*)ptr - virt_base_addrs[process_id]);
#endif
            }
        private:
#if defined (OMNIMEM_BACKEND_DPDK)
            T* ptr_;
#else
            uint64_t offset_;
#endif
        };

        template<typename Type> inline Type& operator*(const Pointer<Type>& p) {
#if defined (OMNIMEM_BACKEND_DPDK)
            return *p.ptr_;
#else
            return *(Type*)(virt_base_addrs[process_id] + p.offset_);
#endif
        }
    }
}

#endif //OMNISTACK_MEMORY_H
