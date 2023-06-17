//
// Created by Jeremy Guo on 2023/6/17.
//

#ifndef OMNISTACK_MEMORY_H
#define OMNISTACK_MEMORY_H

#include <unistd.h>
#include <cstdint>
#include <string>

namespace omnistack {
    namespace mem {
        constexpr uint64_t kMetaHeadroomSize = 64;

        extern uint64_t process_id;
        extern thread_local uint64_t thread_id;

#if defined(OMNIMEM_BACKEND_DPDK)
        static constexpr std::string_view kBackendName = "dpdk";
#else
        static constexpr std::string_view kBackendName = "origin";
#endif
        enum class RegionType {
            kLocal = 0,
            kShared,
            kNamedShared,
            kMempoolChunk
        };

        class MemoryPool;
        struct RegionMeta {
            RegionType type;
            uint64_t iova;
            uint64_t thread_id;
            size_t size;

            union {
                MemoryPool* mempool;
            };
        };

        /**
         * @brief Initialize the memory subsystem per process
         */
        void InitializeSubsystem();

        /**
         * @brief Initialize the memory subsystem per thread
         */
        void InitializeSubsystemThread();

        /**
         * @brief Allocate memory in shared memory by name (Can be used cross process)
         * @param name The name of the memory region
         * @param size
         * @param same_pos Set to true to make all the address to the same name has the same virtual address in all processes
         * @return The pointer if created by other process the same address
         */
        void* AllocateNamedShared(const std::string& name, size_t size, bool same_pos = false);

        void FreeNamedShared(void* ptr);

        /**
         * @brief Allocate Memory in Shared Memory
         * @param size the size of the memory in bytes
         * @return The pointer to the shared region
         */
        void* AllocateShared(size_t size);

        /**
         * @brief Free the memory in shared memory
         * @param ptr The pointer to the shared region
         */
        void FreeShared(void* ptr);

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
        void StartControlPlane();
        void StopControlPlane();

        /**
         * @brief Set the owner thread id of the region pointed by ptr
         * @param ptr The pointer to the region
         * @param thread_id The target thread id
         */
#if defined(_MSC_VER_)
        __forceinline
#elif defined(__GNUC__)
        __inline__ __attribute__((always_inline))
#endif
        void SetOwnerThread(void* ptr, uint64_t thread_id) {
            auto meta = (RegionMeta*) (reinterpret_cast<char*>(ptr) - kMetaHeadroomSize);
            meta->thread_id = thread_id;
        }

        /**
         * @brief This function must be called before returned by Memory Pool
         */
#if defined(_MSC_VER_)
        __forceinline
#elif defined(__GNUC__)
        __inline__ __attribute__((always_inline))
#endif
        static void InitMemoryChunkMeta(void* ptr, MemoryPool* mempool, uint64_t iova = 0, uint64_t size = 0) {
            auto meta = (RegionMeta*) (ptr);
            meta->type = RegionType::kMempoolChunk;
            meta->thread_id = thread_id;
            meta->iova = iova;
            meta->size = size;
            meta->mempool = mempool;
        }

        /**
         * @brief This is the base class of memory pool
         *  The implement must require:
         *      1. Each chunk leave kMetaHeadroomSize before each chunk
         *      2. Each Memory Pool Must has the same virtual address in all process
         *      3. Each chunk must has the same virtual address in all process
         */
        class MemoryPool {
        public:
            virtual void* Get() = 0;
            static void PutBack(void* ptr);
            virtual void Put(void* ptr) = 0;

            virtual uint32_t UsableCount() = 0;
            virtual uint32_t UsedCount() = 0;

            virtual void ThreadDestroy(uint64_t thread_id) = 0;
        private:
        };
    }
}

#endif //OMNISTACK_MEMORY_H
