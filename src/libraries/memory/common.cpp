//
// Created by Jeremy Guo on 2023/6/17.
//

#include <omnistack/memory/memory.h>
#include <omnistack/common/logger.h>
#include <thread>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/un.h>
#include <map>
#include <filesystem>
#include <sys/socket.h>
#include <iostream>
#include <set>
#include <vector>

#if defined (__APPLE__)
#include <sys/event.h>
#else
#include <sys/epoll.h>
#include <mutex>
#include <condition_variable>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <sys/file.h>
#endif

#if defined (OMNIMEM_BACKEND_DPDK)
#include <rte_eal.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_prefetch.h>
#endif

#include <numa.h>

static inline
int readAll(int sockfd, char* buf, size_t len, const bool* stopped = nullptr) {
    int total = 0;
    while (total < len) {
        auto ret = read(sockfd, buf + total, len - total);
        if (ret < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && (stopped == nullptr || !*stopped)) {
                continue;
            } else {
                throw std::runtime_error("read error " + std::to_string(errno));
            }
        } else if (ret == 0) {
            throw std::runtime_error("read EOF");
        } else {
            total += ret;
        }
    }
    return total;
}

static inline
void writeAll(int sockfd, const char* buf, size_t len, const bool* stopped = nullptr) {
    int total = 0;
    while (total < len) {
        auto ret = write(sockfd, buf + total, len - total);
        if (ret < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && (stopped == nullptr || !*stopped)) {
                continue;
            } else {
                throw std::runtime_error("write error");
            }
        } else {
            total += ret;
        }
    }
}

namespace omnistack::memory {
    enum class RpcRequestType {
        kGetProcessId = 0,
        kDestroyProcess,
        kNewThread,
        kDestroyThread,
        kGetMemory,
        kFreeMemory,
        kGetMemoryPool,
        kFreeMemoryPool,
        kThreadBindCPU
    };

    enum class RpcResponseStatus {
        kSuccess = 0,
        kUnknownProcess,
        kUnknownType,
        kInvalidThreadId,
        kInvalidMemory,
        kDoubleFree
    };

    struct RpcResponse {
        int id;
        RpcResponseStatus status;
        union {
            struct {
                uint64_t process_id;
                pid_t pid;
            } get_process_id;
            struct {
                uint64_t thread_id;
            } new_thread;
            struct {
                #if defined (OMNIMEM_BACKEND_DPDK)
                void* addr;
                #else
                uint64_t offset;
                #endif
            } get_memory;
            struct {
                #if defined (OMNIMEM_BACKEND_DPDK)
                void* addr;
                #else
                uint64_t offset;
                #endif
            } get_memory_pool;
        };
    };

    struct RpcRequestMeta {
        bool cond_rpc_finished;
        std::condition_variable cond_rpc_changed;
        std::mutex cond_rpc_lock;
        RpcResponse resp;
    };

    struct RpcRequest {
        RpcRequestType type;
        int id;

        union {
            struct {
                size_t size;
                char name[kMaxNameLength];
                uint64_t thread_id;
            } get_memory;
            struct {
                size_t chunk_size;
                size_t chunk_count;
                char name[kMaxNameLength];
                uint64_t thread_id;
            } get_memory_pool;
            struct {
#if defined (OMNIMEM_BACKEND_DPDK)
                void* addr;
#else
                uint64_t offset;
#endif
            } free_memory;
            struct {
#if defined (OMNIMEM_BACKEND_DPDK)
                void* addr;
#else
                uint64_t offset;
#endif
            } free_memory_pool;
            struct {
                uint64_t thread_id;
                int cpu_idx;
            } thread_bind_cpu;
        };
    };

    uint64_t process_id = ~0;
    pid_t main_process_pid = 0;
    thread_local uint64_t thread_id = ~0;

    void Free(void* ptr) {
        auto meta = (RegionMeta*)(
                reinterpret_cast<char*>(ptr) - kMetaHeadroomSize
                );
        switch (meta->type) {
            case RegionType::kLocal:
                FreeLocal(ptr);
                break;
            case RegionType::kNamedShared:
                FreeNamedShared(ptr);
                break;
            case RegionType::kMempoolChunk:
                MemoryPool::PutBack(ptr);
                break;
        }
    }

    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;

    static int sock_client = 0;
    static int sock_to_control_plane = 0;
    static std::string sock_name;
    static std::string sock_lock_name;
    static int sock_lock_fd = 0;
    static int sock_id = 0;

    static ControlPlaneStatus control_plane_status = ControlPlaneStatus::kStopped;

#if !defined(OMNIMEM_BACKEND_DPDK)
    uint8_t** virt_base_addrs = nullptr;
    static std::string virt_base_addrs_name;
    static int virt_base_addrs_fd = 0;

    static std::string virt_shared_region_name;
    static int virt_shared_region_fd = 0;

    static int virt_shared_region_control_plane_fd = 0;
    static uint8_t* virt_shared_region = nullptr;

    std::set<std::pair<uint64_t, uint64_t> > usable_region;
#endif
    static std::set<RegionMeta*> used_regions;
    static std::map<std::string, RegionMeta*> region_name_to_meta;
    static std::map<RegionMeta*, std::multiset<int> > region_meta_to_fd;
    static std::set<RegionMeta*> used_pools;
    static std::map<std::string, RegionMeta*> pool_name_to_meta;

    /** PROCESS INFORMATION **/
    struct ProcessInfo {
        uint64_t process_id;
    };
    static std::map<int, ProcessInfo> fd_to_process_info;
    static std::set<uint64_t> process_id_used;
    static std::set<uint64_t> thread_id_used;
    static std::map<uint64_t, uint64_t> thread_id_to_fd;
    static std::map<uint64_t, int> thread_id_to_cpu;
    /** PROCESS INFORMATION **/

    void MemoryPool::PutBack(void *ptr) {
        auto meta = (RegionMeta*)( reinterpret_cast<char*>(ptr) - kMetaHeadroomSize );
        if (meta->type == RegionType::kMempoolChunk) {
#if defined (OMNIMEM_BACKEND_DPDK)
            if (meta->mempool != nullptr)
                meta->mempool->Put(ptr);
#else
            if (meta->mempool_offset != ~0)
                ((MemoryPool*)(virt_base_addrs[process_id] + meta->mempool_offset))->Put(ptr);
#endif
            else throw std::runtime_error("Chunk's mem-pool is nullptr");
        } else throw std::runtime_error("Ptr is not a chunk");
    }

    static inline
    RegionMeta* AllocateRegionMeta(size_t size, int thread_cpu, uint64_t thread_id, RegionType region_type = RegionType::kNamedShared) {
        auto aligned_size = (size + kMetaHeadroomSize + 63) / 64 * 64;
#if defined (OMNIMEM_BACKEND_DPDK)
        auto alloc_node = thread_cpu == -1 ? -1 : numa_node_of_cpu(thread_cpu);
        if (alloc_node == -1) {
            OMNI_LOG(common::kWarning) << "Cannot find numa node for thread " << thread_id << " this may cause performance issue.\n";
        }
        auto region_meta = (RegionMeta*)rte_malloc_socket(nullptr, aligned_size, 64, alloc_node);
        if (region_meta == nullptr) {
            OMNI_LOG(common::kFatal)  << "Failed to allocate memory for region meta for DPDK no memory\n";
            exit(1);
        }
        region_meta->addr = region_meta;
        region_meta->iova = rte_mem_virt2iova(region_meta) + kMetaHeadroomSize;
#else
        auto usable_iter = usable_region.lower_bound(
                std::make_pair(aligned_size, 0));
        if (usable_iter == usable_region.end())
            throw std::runtime_error("No usable region for mempool chunks");
        auto region_info = *usable_iter;
        usable_region.erase(usable_iter);
        auto region_meta = reinterpret_cast<RegionMeta*>(
            virt_shared_region + region_info.second
        );
        region_info.first -= aligned_size;
        region_info.second += aligned_size;
        if (region_info.first > 0)
            usable_region.insert(region_info);
        
        
        region_meta->iova = 0;
        region_meta->offset = reinterpret_cast<uint8_t*>(region_meta) - virt_shared_region;
#endif
        region_meta->size = aligned_size;
        region_meta->type = region_type;
        region_meta->process_id = 0;
        region_meta->ref_cnt = 1;
        return region_meta;
    }

    /**
     * @brief Free region_meta to memory by (DPDK / Linux Kernel)
    */
    static inline
    void FreeRegionMeta(RegionMeta* region_meta) {
#if defined (OMNIMEM_BACKEND_DPDK)
        rte_free(region_meta);
#else
        auto pre_iter = usable_region.begin();
        while (pre_iter != usable_region.end() && 
            pre_iter->first + pre_iter->second != region_meta->offset)
            pre_iter ++;
        
        auto bak_iter = usable_region.begin();
        while (bak_iter != usable_region.end() &&
            bak_iter->second != region_meta->offset + region_meta->size)
            bak_iter ++;
        
        auto pre_size = pre_iter == usable_region.end() ? 0 : pre_iter->first;
        auto bak_size = bak_iter == usable_region.end() ? 0 : bak_iter->first;
        if (pre_size) usable_region.erase(pre_iter);
        if (bak_size) usable_region.erase(bak_iter);

        usable_region.insert(
            std::make_pair(
                pre_size + region_meta->size + bak_size,
                region_meta->offset - pre_size
            )
        );
#endif
    }

    static inline
    RpcResponse ControlPlaneFreeMemoryPool(int fd, const RpcRequest& rpc_request) {
        RpcResponse resp;
#if defined (OMNIMEM_BACKEND_DPDK)
        auto region_meta = reinterpret_cast<RegionMeta*>(rpc_request.free_memory_pool.addr);
#else
        auto region_meta = reinterpret_cast<RegionMeta*>(
            virt_shared_region + rpc_request.free_memory_pool.offset
        );
#endif
        auto mempool = reinterpret_cast<MemoryPool*>(
            reinterpret_cast<uint8_t*>(region_meta) + kMetaHeadroomSize
        );
        if (region_meta_to_fd.count(region_meta) && used_pools.count(region_meta)) {
            if (region_meta_to_fd[region_meta].count(fd)) {
                region_meta_to_fd[region_meta].erase(
                    region_meta_to_fd[region_meta].find(fd)
                );
                region_meta->ref_cnt --;
                if (!region_meta->ref_cnt) {
#if defined (OMNIMEM_BACKEND_DPDK)
                    auto block_meta = reinterpret_cast<RegionMeta*>(mempool->batch_block_ptr_);
                    auto chunk_meta = reinterpret_cast<RegionMeta*>(mempool->region_ptr_);
#else
                    auto block_meta = reinterpret_cast<RegionMeta*>(
                        virt_shared_region + mempool->batch_block_offset_
                    );
                    auto chunk_meta = reinterpret_cast<RegionMeta*>(
                        virt_shared_region + mempool->region_offset_
                    );
#endif
                    FreeRegionMeta(block_meta);
                    FreeRegionMeta(chunk_meta);
                    FreeRegionMeta(region_meta);
                    region_meta_to_fd.erase(region_meta);
                    auto iter = std::find_if(pool_name_to_meta.begin(), pool_name_to_meta.end(), 
                        [&](std::pair<std::string, RegionMeta*> arg){return arg.second == region_meta;});
                    if (iter != pool_name_to_meta.end()) {
                        pool_name_to_meta.erase(iter);
                    }
                    if (!used_pools.count(region_meta))
                        resp.status = RpcResponseStatus::kDoubleFree;
                    else {
                        used_pools.erase(region_meta);
                        resp.status = RpcResponseStatus::kSuccess;
                    }
                } else resp.status = RpcResponseStatus::kSuccess;
            } else resp.status = RpcResponseStatus::kInvalidThreadId;
        } else resp.status = RpcResponseStatus::kInvalidMemory;
        return resp;
    }

    static inline
    RpcResponse ControlPlaneFreeShared(int fd, const RpcRequest& rpc_request) {
        RpcResponse resp;
#if defined (OMNIMEM_BACKEND_DPDK)
        auto region_meta = reinterpret_cast<RegionMeta*>(rpc_request.free_memory.addr);
#else
        auto region_meta = reinterpret_cast<RegionMeta*>(
            virt_shared_region + rpc_request.free_memory.offset
        );
#endif
        if (region_meta_to_fd.count(region_meta) && used_regions.count(region_meta)) [[likely]] {
            if (region_meta_to_fd[region_meta].count(fd)) {
                region_meta_to_fd[region_meta].erase(
                    region_meta_to_fd[region_meta].find(fd));
                region_meta->ref_cnt --;
                if (!region_meta->ref_cnt) {
                    FreeRegionMeta(region_meta);
                    region_meta_to_fd.erase(region_meta);
                    auto iter = std::find_if(region_name_to_meta.begin(), region_name_to_meta.end(), 
                        [&](std::pair<std::string, RegionMeta*> arg){return arg.second == region_meta;});
                    if (iter != region_name_to_meta.end())
                        region_name_to_meta.erase(iter);
                    if (!used_regions.count(region_meta))
                        resp.status = RpcResponseStatus::kDoubleFree;
                    else {
                        used_regions.erase(region_meta);
                        resp.status = RpcResponseStatus::kSuccess;
                    }
                    OMNI_LOG(common::kInfo) << "Shared Memory Successfully Freed" << std::endl;
                } else resp.status = RpcResponseStatus::kSuccess;
            } else resp.status = RpcResponseStatus::kInvalidThreadId;
        } else resp.status = RpcResponseStatus::kInvalidMemory;
        return resp;
    }

    void ControlPlane() {
        {
            std::unique_lock<std::mutex> _(control_plane_state_lock);
            control_plane_started = true;
            cond_control_plane_started.notify_all();
        }

        int epfd;
        constexpr int kMaxEvents = 16;
#if defined (__APPLE__)
        struct kevent events[kMaxEvents];
#else
        struct epoll_event events[kMaxEvents];
#endif
        try {
#if defined (__APPLE__)
            epfd = kqueue();
            struct kevent ev{};
            EV_SET(&ev, sock_client, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void *) (intptr_t) sock_client);
            {
                auto ret = kevent(epfd, &ev, 1, nullptr, 0, nullptr);
                if (ret)
                    throw std::runtime_error("Failed to set kevent");
            }
#else
            epfd = epoll_create(128);
            struct epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = sock_client;
            {
                auto ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sock_client, &ev);
                if (ret)
                    throw std::runtime_error("Failed to set epoll " + std::to_string(errno));
            }
#endif
        } catch(std::runtime_error& err) {
            std::cerr << err.what() << std::endl;
            return ;
        }
        control_plane_status = ControlPlaneStatus::kRunning;

        while (!stop_control_plane) {
            int nevents;
#if defined (__APPLE__)
            struct timespec timeout = {
                    .tv_sec = 1,
                    .tv_nsec = 0
            };
            nevents = kevent(epfd, nullptr, 0, events, kMaxEvents, &timeout);
#else
            nevents = epoll_wait(epfd, events, kMaxEvents, 1000);
#endif
            for (int eidx = 0; eidx < nevents; eidx ++) {
                auto& evt = events[eidx];
#if defined (__APPLE__)
                auto fd = (int)(intptr_t)evt.udata;
#else
                auto fd = evt.data.fd;
#endif
                if (fd == sock_client) {
                    int new_fd;
                    do {
                        new_fd = accept(sock_client, nullptr, nullptr);
                        if (new_fd > 0) {
                            /** GET A NEW PROCESS ID **/
                            int new_process_id = 1;
                            while (process_id_used.count(new_process_id))
                                new_process_id ++;
                            if (new_process_id > kMaxProcess) {
                                throw std::runtime_error("Too many processes");
                                return ;
                            }
                            process_id_used.insert(new_process_id);

                            /** INIT PROCESS INFO **/
                            fd_to_process_info[new_fd] = ProcessInfo();
                            auto& info = fd_to_process_info[new_fd];

                            info.process_id = new_process_id;
                            /** SET EVENT DRIVEN **/
                            {
                                auto flags = fcntl(new_fd, F_GETFL);
                                if (flags == -1) {
                                    std::cerr << "Failed to get new fd's flags\n";
                                    return ;
                                }
                                auto ret = fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
                                if (ret == -1) {
                                    std::cerr << "Failed to set new fd's flags\n";
                                    return ;
                                }
                            }
#if defined (__APPLE__)
                            struct kevent ev{};
                            EV_SET(&ev, new_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void *) (intptr_t) new_fd);
                            if (kevent(epfd, &ev, 1, nullptr, 0, nullptr)) {
                                std::cerr << "Failed to set kevent for new process\n";
                                return ;
                            }
#else
                            struct epoll_event ev{};
                            ev.events = EPOLLIN | EPOLLET;
                            ev.data.fd = new_fd;
                            if (epoll_ctl(epfd, EPOLL_CTL_ADD, new_fd, &ev)) {
                                std::cerr << "Failed to set epoll for new process\n";
                                return ;
                            }
#endif
                        }
                    } while (new_fd > 0);

                    if (new_fd == 0) {
                        std::cerr << "Unix socket close unexpectedly\n";
                        return ;
                    }

                    if (errno != EAGAIN) {
                        std::cerr << "Unix socket error not caused by EAGAIN\n";
                        return ;
                    }
                } else {
                    RpcRequest rpc_request{};
                    bool peer_closed = false;
                    try {
                        readAll(fd, reinterpret_cast<char *>(&rpc_request), sizeof(RpcRequest));
                    } catch(std::runtime_error& err_info) {
                        peer_closed = true;
                    }

                    if (!peer_closed) {
                        // A normal Rpc Request
                        RpcResponse resp{};
                        switch (rpc_request.type) {
                            case RpcRequestType::kGetProcessId: {
                                if (fd_to_process_info.count(fd)) {
                                    auto& info = fd_to_process_info[fd];
                                    resp.status = RpcResponseStatus::kSuccess;
                                    resp.get_process_id.process_id = info.process_id;
                                    resp.get_process_id.pid = getpid();
                                } else
                                    resp.status = RpcResponseStatus::kUnknownProcess;
                                break;
                            }
                            case RpcRequestType::kNewThread: {
                                auto& info = fd_to_process_info[fd];
                                resp.new_thread.thread_id = 1;
                                while (thread_id_used.count(resp.new_thread.thread_id) && resp.new_thread.thread_id <= kMaxThread)
                                    resp.new_thread.thread_id ++;
                                if (resp.new_thread.thread_id > kMaxThread)
                                    throw std::runtime_error("Too many threads");
                                thread_id_used.insert(resp.new_thread.thread_id);
                                thread_id_to_fd[resp.new_thread.thread_id] = fd;
                                thread_id_to_cpu[resp.new_thread.thread_id] = -1;
                                resp.status = RpcResponseStatus::kSuccess;
                                break;
                            }
                            case RpcRequestType::kGetMemory: {
                                auto region_name = std::string(rpc_request.get_memory.name);
                                auto thread_cpu = thread_id_to_cpu[rpc_request.get_memory.thread_id];
                                if (region_name == "" || region_name_to_meta.count(region_name) == 0) {
                                    auto local_meta = AllocateRegionMeta(rpc_request.get_memory.size, thread_cpu, rpc_request.get_memory.thread_id);
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    resp.get_memory.offset = reinterpret_cast<uint8_t*>(local_meta) - virt_shared_region;
#else
                                    resp.get_memory.addr = local_meta->addr;
#endif
                                    used_regions.insert(local_meta);
                                    region_meta_to_fd[local_meta].insert(fd);
                                    resp.status = RpcResponseStatus::kSuccess;
                                    if (region_name != "")
                                        region_name_to_meta[region_name] = local_meta;
                                } else {
                                    auto& meta = region_name_to_meta[region_name];
                                    meta->ref_cnt ++;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    resp.get_memory.offset = reinterpret_cast<uint8_t*>(meta) - virt_shared_region;
#else
                                    resp.get_memory.addr = meta->addr;
#endif
                                    region_meta_to_fd[meta].insert(fd);
                                    resp.status = RpcResponseStatus::kSuccess;
                                }
                                break;
                            }
                            case RpcRequestType::kGetMemoryPool: {
                                auto pool_name = std::string(rpc_request.get_memory_pool.name);
                                auto thread_cpu = thread_id_to_cpu[rpc_request.get_memory_pool.thread_id];
                                if (pool_name == "" || pool_name_to_meta.count(pool_name) == 0) {
                                    RegionMeta* mempool_meta = AllocateRegionMeta(sizeof(MemoryPool), thread_cpu, rpc_request.get_memory_pool.thread_id, RegionType::kMempool);
                                    used_pools.insert(mempool_meta);
#if defined (OMNIMEM_BACKEND_DPDK)
                                    resp.get_memory_pool.addr = mempool_meta->addr;
#else
                                    resp.get_memory_pool.offset = mempool_meta->offset;
#endif
                                    region_meta_to_fd[mempool_meta].insert(fd);
                                    resp.status = RpcResponseStatus::kSuccess;

                                    auto mempool = reinterpret_cast<MemoryPool*>(reinterpret_cast<uint8_t*>(mempool_meta) + kMetaHeadroomSize);
                                    mempool->chunk_size_ = (rpc_request.get_memory_pool.chunk_size + 63 + kMetaHeadroomSize) / 64 * 64;
                                    mempool->chunk_count_ = (rpc_request.get_memory_pool.chunk_count + 63) / 64 * 64;
                                    pthread_mutexattr_t init_attr;
                                    pthread_mutexattr_init(&init_attr);
                                    pthread_mutexattr_setpshared(&init_attr, PTHREAD_PROCESS_SHARED);
                                    pthread_mutex_init(&mempool->recycle_mutex_, &init_attr);
                                    pthread_mutexattr_destroy(&init_attr);

                                    auto aligned_chunk_region_size = mempool->chunk_size_ * mempool->chunk_count_;
                                    auto chunk_meta = AllocateRegionMeta(aligned_chunk_region_size, thread_cpu, rpc_request.get_memory_pool.thread_id);
#if defined (OMNIMEM_BACKEND_DPDK)
                                    mempool->region_ptr_ = reinterpret_cast<uint8_t*>(chunk_meta);
#else
                                    mempool->region_offset_ = reinterpret_cast<uint8_t*>(chunk_meta) - virt_shared_region;
#endif
                                    { // Allocate & Set Blocks
                                        auto block_need_allocate = (mempool->chunk_count_ + kMemoryPoolLocalCache) / kMemoryPoolLocalCache + 2 * kMaxThread + 4;
                                        auto all_block_size = block_need_allocate * sizeof(MemoryPoolBatch);
                                        auto block_meta = AllocateRegionMeta(all_block_size, thread_cpu, rpc_request.get_memory_pool.thread_id);
#if defined (OMNIMEM_BACKEND_DPDK)
                                        auto chunk_region_begin = mempool->region_ptr_ + kMetaHeadroomSize;
                                        mempool->batch_block_ptr_ = reinterpret_cast<uint8_t*>(block_meta);
                                        mempool->full_block_ptr_ = nullptr;
                                        mempool->empty_block_ptr_ = nullptr;
                                        auto current_block = reinterpret_cast<MemoryPoolBatch*>(mempool->batch_block_ptr_ + kMetaHeadroomSize);
                                        int used_chunk = 0;
                                        for (int i = 0; i < mempool->chunk_count_; i += kMemoryPoolLocalCache, used_chunk ++) {
                                            current_block->cnt = std::min(mempool->chunk_count_ - i, kMemoryPoolLocalCache);
                                            current_block->used = 0;
                                            for (int j = 0; j < current_block->cnt; j ++) {
                                                auto chunk = chunk_region_begin + (i + j) * mempool->chunk_size_;
                                                auto single_chunk_meta = reinterpret_cast<RegionMeta*>(chunk);
                                                single_chunk_meta->iova = (chunk - reinterpret_cast<uint8_t*>(chunk_meta)) + memory::GetIova(chunk_meta);
                                                single_chunk_meta->mempool = mempool;
                                                single_chunk_meta->addr = (void*)single_chunk_meta;
                                                single_chunk_meta->size = mempool->chunk_size_;
                                                single_chunk_meta->process_id = 0;
                                                single_chunk_meta->type = RegionType::kMempoolChunk;
                                                single_chunk_meta->ref_cnt = 1;
                                                current_block->addrs[j] = chunk;
                                            }
                                            current_block->next = mempool->full_block_ptr_;
                                            mempool->full_block_ptr_ = current_block;
                                            current_block ++;
                                        }
                                        for (;used_chunk < block_need_allocate; used_chunk ++) {
                                            current_block->next = mempool->empty_block_ptr_;
                                            mempool->empty_block_ptr_ = current_block;
                                            current_block ++;
                                        }
#else
                                        auto chunk_region_begin = mempool->region_offset_ + kMetaHeadroomSize;
                                        mempool->batch_block_offset_ = reinterpret_cast<uint8_t*>(block_meta) - virt_shared_region;
                                        mempool->full_block_offset_ = ~0;
                                        mempool->empty_block_offset_ = ~0;
                                        auto current_block = reinterpret_cast<MemoryPoolBatch*>(virt_shared_region + mempool->batch_block_offset_ + kMetaHeadroomSize);
                                        int used_chunk = 0;
                                        for (int i = 0; i < mempool->chunk_count_; i += kMemoryPoolLocalCache, used_chunk ++) {
                                            current_block->cnt = std::min(mempool->chunk_count_ - i, kMemoryPoolLocalCache);
                                            current_block->used = 0;
                                            for (int j = 0; j < current_block->cnt; j ++) {
                                                auto chunk = chunk_region_begin + (i + j) * mempool->chunk_size_;
                                                auto single_chunk_meta = reinterpret_cast<RegionMeta*>(chunk + virt_shared_region);
                                                single_chunk_meta->iova = 0;
                                                single_chunk_meta->mempool_offset = (uint8_t*)mempool - virt_shared_region;
                                                single_chunk_meta->offset = chunk;
                                                single_chunk_meta->size = mempool->chunk_size_;
                                                single_chunk_meta->process_id = 0;
                                                single_chunk_meta->type = RegionType::kMempoolChunk;
                                                single_chunk_meta->ref_cnt = 1;
                                                current_block->offsets[j] = chunk;
                                            }
                                            current_block->next = mempool->full_block_offset_;
                                            mempool->full_block_offset_ = reinterpret_cast<uint8_t*>(current_block) - virt_shared_region;
                                            current_block ++;
                                        }
                                        for (;used_chunk < block_need_allocate; used_chunk ++) {
                                            current_block->next = mempool->empty_block_offset_;
                                            mempool->empty_block_offset_ = reinterpret_cast<uint8_t*>(current_block) - virt_shared_region;
                                            current_block ++;
                                        }
#endif
                                        for (int i = 0; i <= kMaxThread; i ++) {
                                            mempool->local_cache_[i] = nullptr;
                                            mempool->local_free_cache_[i] = nullptr;
                                        }
                                    }
                                    
                                    if (pool_name != "")
                                        pool_name_to_meta[pool_name] = mempool_meta;
                                } else {
                                    auto& meta = pool_name_to_meta[pool_name];
                                    meta->ref_cnt ++;
                                    region_meta_to_fd[meta].insert(fd);
#if defined (OMNIMEM_BACKEND_DPDK)
                                    resp.get_memory_pool.addr = meta->addr;
#else
                                    resp.get_memory_pool.offset = meta->offset;
#endif
                                    resp.status = RpcResponseStatus::kSuccess;
                                }
                                break;
                            }
                            case RpcRequestType::kThreadBindCPU: {
                                auto thread_id = rpc_request.thread_bind_cpu.thread_id;
                                if (thread_id_to_fd[thread_id] != fd) {
                                    resp.status = RpcResponseStatus::kInvalidThreadId;
                                    break;
                                }
                                thread_id_to_cpu[thread_id] = rpc_request.thread_bind_cpu.cpu_idx;
                                resp.status = RpcResponseStatus::kSuccess;
                                break;
                            }
                            case RpcRequestType::kFreeMemory: {
                                resp = ControlPlaneFreeShared(fd, rpc_request);
                                break;
                            }
                            case RpcRequestType::kFreeMemoryPool: {
                                resp = ControlPlaneFreeMemoryPool(fd, rpc_request);
                                break;
                            }
                            default:
                                resp.status = RpcResponseStatus::kUnknownType;
                        }

                        resp.id = rpc_request.id;
                        try {
                            writeAll(fd, reinterpret_cast<char *>(&resp), sizeof(RpcResponse));
                        } catch(std::runtime_error& err_info) {
                            peer_closed = true;
                        }
                    }

                    if (peer_closed) {
                        std::vector<RegionMeta*> need_free;
                        std::vector<std::pair<RegionMeta*, uint64_t> > unique_free;
                        std::set<int> thread_ids;
                        auto& proc_info = fd_to_process_info[fd];
                        for (auto& iter : thread_id_to_fd) {
                            if (iter.second == fd) {
                                thread_ids.insert(iter.first);
                            }
                        }
                        for (auto& tid : thread_ids) {
                            thread_id_used.erase(tid);
                            thread_id_to_cpu.erase(tid);
                            thread_id_to_fd.erase(tid);
                        }
                        process_id_used.erase(proc_info.process_id);
                        for (auto& region_fds : region_meta_to_fd) {
                            auto free_times = region_fds.second.count(fd);
                            for (int it = 0; it < free_times; it ++) {
                                need_free.emplace_back(region_fds.first);
                            }
                            if (free_times) unique_free.emplace_back(
                                std::make_pair(region_fds.first, free_times)
                            );
                        }
                        for (auto& region_meta_pair : unique_free) {
                            if (thread_ids.empty())
                                throw std::runtime_error("A process must have thread");
                            auto& region_meta = region_meta_pair.first;
                            auto& region_times = region_meta_pair.second;
                            if (region_meta->ref_cnt > region_times && region_meta->type == RegionType::kMempool) {
                                /// TODO: check all the chunk 
                            }
                        }
                        for (auto& region_meta : need_free) {
                            switch (region_meta->type)
                            {
                            case RegionType::kMempool: {
                                RpcRequest request = {
                                    .type = RpcRequestType::kFreeMemoryPool,
                                    .free_memory_pool = {
#if defined (OMNIMEM_BACKEND_DPDK)
                                        .addr = region_meta
#else
                                        .offset = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(region_meta) - virt_shared_region)
#endif
                                    }
                                };
                                auto status = ControlPlaneFreeMemoryPool(fd, request).status;
                                if (status != RpcResponseStatus::kSuccess)
                                    throw std::runtime_error("Failed to recycle mempool when process crash " + std::to_string((int)status));
                                break;
                            }
                            case RegionType::kNamedShared: {
                                RpcRequest request = {
                                    .type = RpcRequestType::kFreeMemory,
                                    .free_memory = {
#if defined (OMNIMEM_BACKEND_DPDK)
                                        .addr = region_meta
#else
                                        .offset = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(region_meta) - virt_shared_region)
#endif
                                    }
                                };
                                if (ControlPlaneFreeShared(fd, request).status != RpcResponseStatus::kSuccess)
                                    throw std::runtime_error("Failed to recycle shared when process crash");
                                break;
                            }
                            default:
                                throw std::runtime_error("Failed to recycle resources when process crash");
                                break;
                            }
                        }
#if !defined(OMNIMEM_BACKEND_DPDK)
                        for (auto& tid : thread_ids)
                            virt_base_addrs[tid] = nullptr;
#endif
                        close(fd);
                    }
                }
            }
        }

        control_plane_status = ControlPlaneStatus::kStopped;
    }

    void StartControlPlane(
#if defined (OMNIMEM_BACKEND_DPDK)
        bool init_dpdk
#endif
    ) {
        control_plane_status = ControlPlaneStatus::kStarting;
        std::unique_lock<std::mutex> _(control_plane_state_lock);
        if (control_plane_started)
            throw std::runtime_error("There is multiple control plane");
#if defined (OMNIMEM_BACKEND_DPDK)
        if (init_dpdk) {
            constexpr int argc = 4;
            char** argv = new char*[argc];
            for (int i = 0; i < argc; i ++)
                argv[i] = new char[256];
            strcpy(argv[0], "./_");
            strcpy(argv[1], "--log-level");
            strcpy(argv[2], "warning");
            strcpy(argv[3], "--proc-type=auto");
            if (rte_eal_init(argc, argv) < 0)
                throw std::runtime_error("Failed to init dpdk");
        }
#endif
#if !defined(OMNIMEM_BACKEND_DPDK)
        {
            virt_base_addrs_name = "omnistack_virt_base_addrs_" + std::to_string(getpid());
            {
                #if defined (__APPLE__)
                auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
                #else
                auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR, 0666);
                #endif
                if (ret >= 0) throw std::runtime_error("Failed to init virt_base_addrs for already exists");
            }
            {
                #if defined (__APPLE__)
                virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR | O_CREAT);
                #else
                virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR | O_CREAT, 0666);
                #endif
                if (virt_base_addrs_fd < 0) throw std::runtime_error("Failed to init virt_base_addrs");
            }
            {
                auto ret = ftruncate(virt_base_addrs_fd, kMaxProcess * sizeof(uint8_t *));
                if (ret) throw std::runtime_error("Failed to ftruncate virt_base_addrs");
            }
            {
                virt_base_addrs =
                        reinterpret_cast<uint8_t **>(mmap(nullptr, kMaxProcess * sizeof(uint8_t *),
                                                          PROT_WRITE | PROT_READ,
                                                          MAP_SHARED, virt_base_addrs_fd, 0));
                if (!virt_base_addrs)
                    throw std::runtime_error("Failed to create virt_base_addrs");
            }
        }

        {
            virt_shared_region_name = "omnistack_virt_shared_region_" + std::to_string(getpid());
            {
                #if defined (__APPLE__)
                auto ret = shm_open(virt_shared_region_name.c_str(), O_RDWR);
                #else
                auto ret = shm_open(virt_shared_region_name.c_str(), O_RDWR, 0666);
                #endif
                if (ret >= 0) throw std::runtime_error("Failed to init virt_shared_region for already exists");
            }
            {
                #if defined (__APPLE__)
                virt_shared_region_control_plane_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR | O_CREAT);
                #else
                virt_shared_region_control_plane_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR | O_CREAT, 0666);
                #endif
                if (virt_shared_region_control_plane_fd < 0) throw std::runtime_error("Failed to init virt_shared_region");
            }
            {
                auto ret = ftruncate(virt_shared_region_control_plane_fd, kMaxTotalAllocateSize);
                if (ret) throw std::runtime_error("ControlPlane: Failed to ftruncate virt_shared_region");
            }
            virt_shared_region = reinterpret_cast<uint8_t *>(mmap(nullptr, kMaxTotalAllocateSize,
                                                                           PROT_WRITE | PROT_READ,
                                                                           MAP_SHARED, virt_shared_region_control_plane_fd, 0));
            if (!virt_shared_region)
                throw std::runtime_error("Failed to get virt_shared_region");

            usable_region.clear();
            usable_region.insert(std::make_pair(kMaxTotalAllocateSize, 0));
        }
#endif

        {
            for (sock_id = 0; sock_id < kMaxControlPlane; sock_id ++) {
                sock_lock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                                 std::to_string(sock_id) + ".lock";
                sock_lock_fd = open(sock_lock_name.c_str(), O_WRONLY | O_CREAT, 0644);
                if (sock_lock_fd < 0)
                    continue;
                if (flock(sock_lock_fd, LOCK_EX | LOCK_NB)) {
                    close(sock_lock_fd);
                    continue;
                }

                sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                            std::to_string(sock_id) + ".socket";
                break;
            }

            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            if (sock_name.length() >= sizeof(addr.sun_path))
                throw std::runtime_error("Failed to assign sock path to unix domain addr");
            strcpy(addr.sun_path, sock_name.c_str());
            sock_client = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock_client < 0)
                throw std::runtime_error("Failed to create unix socket");
            std::filesystem::remove(sock_name);
            if (bind(sock_client, (struct sockaddr*)&addr,
                     sock_name.length() + sizeof(addr.sun_family))) {
                throw std::runtime_error("Failed to bind unix socket" + sock_name);
            }
            if (listen(sock_client, kMaxIncomingProcess)) {
                throw std::runtime_error("Failed to listen unix socket");
            }
            {
                auto flags = fcntl(sock_client, F_GETFL);
                if (flags == -1)
                    throw std::runtime_error("Faileld to get unix socket flags");
                auto ret = fcntl(sock_client, F_SETFL, (flags | O_NONBLOCK));
                if (ret == -1)
                    throw std::runtime_error("Failed to set unix socket flags");
            }
        }

        control_plane_thread = new std::thread(ControlPlane);
        cond_control_plane_started.wait(_, [&](){
            return !control_plane_started;
        });
    }

    void StopControlPlane() {
        stop_control_plane = true;

        if (control_plane_thread != nullptr) {
            control_plane_thread->join();
            delete control_plane_thread;
            control_plane_thread = nullptr;
        }

#if !defined(OMNIMEM_BACKEND_DPDK)
        {
            // Destroy Virt Base Addr
            {
                auto ret = shm_unlink(virt_base_addrs_name.c_str());
                if (ret) throw std::runtime_error("Failed to unlink virt_base_addr");
                virt_base_addrs_name = "";
            }
            if (virt_base_addrs != nullptr) {
                auto ret = munmap(virt_base_addrs, kMaxProcess * sizeof(uint8_t *));
                if (ret) throw std::runtime_error("Failed to unmap virt_base_addr");
                virt_base_addrs = nullptr;
            }
            if (virt_base_addrs_fd != 0) {
                close(virt_base_addrs_fd);
                virt_base_addrs_fd = 0;
            }
        }
#endif

        {
            if (sock_client > 0) {
                close(sock_client);
                sock_client = 0;
            }

            for (auto& process : fd_to_process_info)
                close(process.first);

            fd_to_process_info.clear();
        }

        control_plane_started = false;
        stop_control_plane = false;
    }

    static std::thread* rpc_response_receiver;
    static std::map<int, RpcRequestMeta*> id_to_rpc_meta;
    static thread_local RpcRequest local_rpc_request{};
    static thread_local RpcRequestMeta local_rpc_meta{};
    static std::mutex rpc_request_lock;
    static int rpc_id;

    static void RpcResponseReceiver() {
        RpcResponse resp{};
        while (true) {
            readAll(sock_to_control_plane, reinterpret_cast<char*>(&resp), sizeof(RpcResponse));
            {
                std::unique_lock<std::mutex> _1(rpc_request_lock);
                if (id_to_rpc_meta.count(resp.id)) {
                    auto meta = id_to_rpc_meta[resp.id];
                    std::unique_lock<std::mutex> _(meta->cond_rpc_lock);

                    meta->cond_rpc_finished = true;
                    meta->cond_rpc_changed.notify_all();
                    meta->resp = resp;
                    id_to_rpc_meta.erase(resp.id);
                }
            }
        }
    }

    static RpcResponse SendLocalRpcRequest() {
        {
            std::unique_lock<std::mutex> _(rpc_request_lock);
            local_rpc_request.id = ++rpc_id;
            while (id_to_rpc_meta.count(local_rpc_request.id))
                local_rpc_request.id = ++rpc_id;
            id_to_rpc_meta[local_rpc_request.id] = &local_rpc_meta;
            local_rpc_meta.cond_rpc_finished = false;
            writeAll(sock_to_control_plane, reinterpret_cast<char*>(&local_rpc_request), sizeof(RpcRequest));
        }

        {
            std::unique_lock<std::mutex> _(local_rpc_meta.cond_rpc_lock);
            local_rpc_meta.cond_rpc_changed.wait(_, [](){
                return local_rpc_meta.cond_rpc_finished;
            });
        }

        return local_rpc_meta.resp;
    }

    void InitializeSubsystem(int control_plane_id
#if defined (OMNIMEM_BACKEND_DPDK)
            ,bool init_dpdk
#endif
        ) {
#if defined (OMNIMEM_BACKEND_DPDK)
        if (init_dpdk) {
            constexpr int argc = 4;
            char** argv = new char*[argc];
            for (int i = 0; i < argc; i ++)
                argv[i] = new char[256];
            strcpy(argv[0], "./_");
            strcpy(argv[1], "--log-level");
            strcpy(argv[2], "warning");
            strcpy(argv[3], "--proc-type=secondary");
            if (rte_eal_init(argc, argv) < 0)
                throw std::runtime_error("Failed to init dpdk");
        }
#endif
        rpc_id = 0;
        sock_id = control_plane_id;
        sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                    std::to_string(sock_id) + ".socket";
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, sock_name.c_str());
        sock_to_control_plane = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_to_control_plane, (struct sockaddr*)&addr, sizeof(addr.sun_family) + sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane " + std::to_string(errno));

        rpc_response_receiver = new std::thread(RpcResponseReceiver);

        local_rpc_request.type = RpcRequestType::kGetProcessId;
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            process_id = resp.get_process_id.process_id;
            main_process_pid = resp.get_process_id.pid;
        } else {
            std::cerr << "Failed to initialize subsystem\n";
            exit(1);
        }
#if !defined(OMNIMEM_BACKEND_DPDK)
        if (getpid() != main_process_pid) {
            {
                virt_base_addrs_name = "omnistack_virt_base_addrs_" + std::to_string(main_process_pid);
                {
                    #if defined (__APPLE__)
                    virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
                    #else
                    virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR, 0666);
                    #endif
                    if (virt_base_addrs_fd < 0) throw std::runtime_error("Failed to init virt_base_addrs");
                }
                {
                    auto ret = ftruncate(virt_base_addrs_fd, kMaxProcess * sizeof(uint8_t *));
                    if (ret) throw std::runtime_error("Failed to ftruncate virt_base_addrs");
                }
                {
                    virt_base_addrs =
                            reinterpret_cast<uint8_t **>(mmap(nullptr, kMaxProcess * sizeof(uint8_t *),
                                                              PROT_WRITE | PROT_READ,
                                                              MAP_SHARED, virt_base_addrs_fd, 0));
                    if (!virt_base_addrs)
                        throw std::runtime_error("Failed to create virt_base_addrs");
                }
            } // Init Virt Base Addr

            {
                virt_shared_region_name = "omnistack_virt_shared_region_" + std::to_string(main_process_pid);
                {
                    #if defined (__APPLE__)
                    virt_shared_region_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR);
                    #else
                    virt_shared_region_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR, 0666);
                    #endif
                    if (virt_shared_region_fd < 0) throw std::runtime_error("Failed to init virt_shared_region");
                }
                {
                    auto ret = ftruncate(virt_shared_region_fd, kMaxTotalAllocateSize);
                    if (ret) throw std::runtime_error("Failed to ftruncate virt_shared_region");
                }
                virt_base_addrs[process_id] = reinterpret_cast<uint8_t *>(mmap(nullptr, kMaxTotalAllocateSize,
                                                                               PROT_WRITE | PROT_READ,
                                                                               MAP_SHARED, virt_shared_region_fd, 0));
                if (!virt_base_addrs[process_id])
                    throw std::runtime_error("Failed to get virt_shared_region");
            }
        } else {
            virt_base_addrs[process_id] = reinterpret_cast<uint8_t *>(virt_shared_region);
        }
#endif
    }

    void InitializeSubsystemThread() {
        local_rpc_request.type = RpcRequestType::kNewThread;
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            thread_id = resp.new_thread.thread_id;
        } else {
            std::cerr << "Failed to initialize subsystem per thread\n";
            exit(1);
        }
    }

    /**
         * @brief Allocate Memory in Local Memory
         */
    void* AllocateLocal(size_t size) {
        return malloc(size);
    }

    /**
     * @brief Free the memory in local memory
     * @param ptr The pointer to the memory region
     */
    void FreeLocal(void* ptr) {
        free(ptr);
    }

    /**
         * @brief Allocate memory in shared memory by name (Can be used cross process)
         * @param name The name of the memory region, empty string "" means anonymous memory region
         * @param size
         * @param same_pos Set to true to make all the address to the same name has the same virtual address in all processes
         * @return The pointer if created by other process the same address
         */
    void* AllocateNamedShared(const std::string& name, size_t size) {
        local_rpc_request.type = RpcRequestType::kGetMemory;
        if (name.length() >= kMaxNameLength) throw std::runtime_error("Name too long");
        local_rpc_request.get_memory.size = size;
        local_rpc_request.get_memory.thread_id = thread_id;
        strcpy(local_rpc_request.get_memory.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            #if defined (OMNIMEM_BACKEND_DPDK)
            auto meta = reinterpret_cast<RegionMeta*>(resp.get_memory.addr);
            #else
            auto meta = reinterpret_cast<RegionMeta*>(virt_base_addrs[process_id] + resp.get_memory.offset);
            #endif
            return reinterpret_cast<uint8_t*>(meta) + kMetaHeadroomSize;
        }
        return nullptr;
    }

    static thread_local int current_cpu = -1;
    static thread_local int current_node = -1;

    /**
     * @brief Calling from running thread to only report that you have binded to a cpu core not used to bind cpu
    */
    void BindedCPU(int cpu) {
        local_rpc_request.type = RpcRequestType::kThreadBindCPU;
        local_rpc_request.thread_bind_cpu.cpu_idx = cpu;
        local_rpc_request.thread_bind_cpu.thread_id = thread_id;
        auto resp = SendLocalRpcRequest();
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to report binding cpu");
        
        current_cpu = cpu;
        current_node = numa_node_of_cpu(cpu);
    }

    void FreeNamedShared(void* ptr) {
        local_rpc_request.type = RpcRequestType::kFreeMemory;
#if defined (OMNIMEM_BACKEND_DPDK)
        local_rpc_request.free_memory.addr = (uint8_t*)ptr - kMetaHeadroomSize;
#else
        local_rpc_request.free_memory.offset = reinterpret_cast<uint8_t*>(ptr) - virt_base_addrs[process_id] - kMetaHeadroomSize;
#endif
        auto resp = SendLocalRpcRequest();
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to free shared memory");
    }

    RegionMeta* MemoryPool::GetChunkMeta() {
#if defined(OMNIMEM_BACKEND_DPDK)
        auto meta = reinterpret_cast<RegionMeta*>(region_ptr_);
#else
        auto meta = reinterpret_cast<RegionMeta*>(virt_base_addrs[process_id] + region_offset_);
#endif
        return meta;
    }

    void MemoryPool::Put(void* ptr) {
        auto real_ptr = reinterpret_cast<uint8_t*>(ptr) - kMetaHeadroomSize;
        auto cache = local_free_cache_[thread_id];
        if (!cache) [[unlikely]] {
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            auto container_ptr = empty_block_ptr_;
            empty_block_ptr_ = container_ptr->next;
#else
            auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                     empty_block_offset_);
            empty_block_offset_ = container_ptr->next;
#endif
            pthread_mutex_unlock(&recycle_mutex_);

            local_free_cache_[thread_id] = cache = container_ptr;
            container_ptr->used = container_ptr->cnt = 0;
        } else if (cache->cnt == kMemoryPoolLocalCache) [[unlikely]] { // Get a empty block from main pool
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            cache->next = full_block_ptr_;
            full_block_ptr_ = cache;
#else
            cache->next = full_block_offset_;
            full_block_offset_ = reinterpret_cast<uint8_t*>(cache) - virt_base_addrs[process_id];
#endif

#if defined (OMNIMEM_BACKEND_DPDK)
            auto container_ptr = empty_block_ptr_;
            empty_block_ptr_ = container_ptr->next;
#else
            auto container_ptr = reinterpret_cast<MemoryPoolBatch*>(virt_base_addrs[process_id] + empty_block_offset_);
            empty_block_offset_ = container_ptr->next;
#endif
            pthread_mutex_unlock(&recycle_mutex_);

            local_free_cache_[thread_id] = cache = container_ptr;
            container_ptr->used = container_ptr->cnt = 0;
        }
        if (cache) [[likely]] {
            reinterpret_cast<RegionMeta*>(real_ptr)->ref_cnt = 0;
#if defined (OMNIMEM_BACKEND_DPDK)
            cache->addrs[cache->cnt++] = real_ptr;
#else
            cache->offsets[cache->cnt++] = (real_ptr - virt_base_addrs[process_id]);
#endif
        } else throw std::runtime_error("Cache is nullptr");
    }

    void* MemoryPool::Get() {
        if (local_cache_[thread_id] == nullptr) [[unlikely]] {
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            if (full_block_ptr_ != nullptr) {
                auto container_ptr = full_block_ptr_;
                full_block_ptr_ = container_ptr->next;

                local_cache_[thread_id] = container_ptr;
            }
#else
            if (full_block_offset_ != ~0) {
                auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                         full_block_offset_);
                full_block_offset_ = container_ptr->next;

                local_cache_[thread_id] = container_ptr;
            }
#endif
            pthread_mutex_unlock(&recycle_mutex_);
        } else if (local_cache_[thread_id]->used == local_cache_[thread_id]->cnt) [[unlikely]] { // chunk ran out
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            if (full_block_ptr_ != nullptr) {
                auto container_ptr = full_block_ptr_;
                full_block_ptr_ = container_ptr->next;

                local_cache_[thread_id]->next = empty_block_ptr_;
                empty_block_ptr_ = local_cache_[thread_id];


                /// TODO: prefetch

                local_cache_[thread_id] = container_ptr;
            }
#else
            if (full_block_offset_ != ~0) {
                auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                         full_block_offset_);
                full_block_offset_ = container_ptr->next;

                local_cache_[thread_id]->next = empty_block_offset_;
                empty_block_offset_ =
                        reinterpret_cast<uint8_t *>(local_cache_[thread_id]) - virt_base_addrs[process_id];


                /// TODO: prefetch

                local_cache_[thread_id] = container_ptr;
                
            }
#endif
            else local_cache_[thread_id] = nullptr;
            pthread_mutex_unlock(&recycle_mutex_);
        }
        if (local_cache_[thread_id] != nullptr) [[likely]] {
#if defined (OMNIMEM_BACKEND_DPDK)
            auto ret = local_cache_[thread_id]->addrs[local_cache_[thread_id]->used++];
#else
            auto ret_offset = local_cache_[thread_id]->offsets[local_cache_[thread_id]->used++];
            auto ret = virt_base_addrs[process_id] + ret_offset;
#endif
            return (char*)ret + kMetaHeadroomSize;
        }
        return nullptr;
    }

    void MemoryPool::DoRecycle() {
        if (local_cache_[thread_id] == nullptr) [[unlikely]] {
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            if (full_block_ptr_ != nullptr) {
                auto container_ptr = full_block_ptr_;
                full_block_ptr_ = container_ptr->next;

                /* prefetch all the memory region */
                // for(uint32_t i = 0; i < container_ptr->cnt; i ++) {
                //     rte_prefetch0(container_ptr->addrs[i]);
                //     rte_prefetch0(container_ptr->addrs[i] + kMetaHeadroomSize);
                // }

                local_cache_[thread_id] = container_ptr;
            }
#else
            if (full_block_offset_ != ~0) {
                auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                         full_block_offset_);
                full_block_offset_ = container_ptr->next;

                /// TODO: prefetch

                local_cache_[thread_id] = container_ptr;
            }
#endif
            pthread_mutex_unlock(&recycle_mutex_);
        } else if (local_cache_[thread_id]->used == local_cache_[thread_id]->cnt) [[unlikely]] { // chunk ran out
            pthread_mutex_lock(&recycle_mutex_);
#if defined (OMNIMEM_BACKEND_DPDK)
            if (full_block_ptr_ != nullptr) {
                auto container_ptr = full_block_ptr_;
                full_block_ptr_ = container_ptr->next;

                local_cache_[thread_id]->next = empty_block_ptr_;
                empty_block_ptr_ = local_cache_[thread_id];


                /* prefetch all the memory region */
                // for(uint32_t i = 0; i < container_ptr->cnt; i ++) {
                //     rte_prefetch0(container_ptr->addrs[i]);
                //     rte_prefetch0(container_ptr->addrs[i] + kMetaHeadroomSize);
                // }

                local_cache_[thread_id] = container_ptr;
            }
#else
            if (full_block_offset_ != ~0) {
                auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                         full_block_offset_);
                full_block_offset_ = container_ptr->next;

                local_cache_[thread_id]->next = empty_block_offset_;
                empty_block_offset_ =
                        reinterpret_cast<uint8_t *>(local_cache_[thread_id]) - virt_base_addrs[process_id];


                /// TODO: prefetch

                local_cache_[thread_id] = container_ptr;
                
            }
#endif
            else local_cache_[thread_id] = nullptr;
            pthread_mutex_unlock(&recycle_mutex_);
        }
    }

    inline void MemoryPool::GetEnough(int size, void* ptrs[]) {
        for (int i = 0; i < size; i ++) {
#if defined (OMNIMEM_BACKEND_DPDK)
            auto ret = local_cache_[thread_id]->addrs[local_cache_[thread_id]->used++];
#else
            auto ret_offset = local_cache_[thread_id]->offsets[local_cache_[thread_id]->used++];
            auto ret = virt_base_addrs[process_id] + ret_offset;
#endif
            ptrs[i] = (char*)ret + kMetaHeadroomSize;
        }
    }

    int MemoryPool::Get(int size, void* ptrs[]) {
        if (size > kMemoryPoolLocalCache) [[unlikely]] {
            for (int i = 0; i < size; i ++) {
                auto ret = Get();
                if (ret == nullptr) [[unlikely]] {
                    return i;
                }
                ptrs[i] = ret;
            }
            return size;
        }
        if (local_cache_[thread_id] == nullptr) [[unlikely]] {
            DoRecycle();
        }
        if (local_cache_[thread_id]->cnt - local_cache_[thread_id]->used >= size) [[likely]] {
            GetEnough(size, ptrs);
            return size;
        }
        auto first = local_cache_[thread_id]->cnt - local_cache_[thread_id]->used;
        auto second = size - first;
        GetEnough(first, ptrs);
        DoRecycle();
        GetEnough(second, ptrs + first);
        return size;
    }

    MemoryPool* AllocateMemoryPool(const std::string& name, size_t chunk_size, size_t chunk_count) {
        local_rpc_request.type = RpcRequestType::kGetMemoryPool;
        if (name.length() >= kMaxNameLength) throw std::runtime_error("Name too long");
        local_rpc_request.get_memory_pool.chunk_size = chunk_size;
        local_rpc_request.get_memory_pool.chunk_count = chunk_count;
        local_rpc_request.get_memory_pool.thread_id = thread_id;
        strcpy(local_rpc_request.get_memory_pool.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
#if defined (OMNIMEM_BACKEND_DPDK)
            auto pool = reinterpret_cast<MemoryPool*>(reinterpret_cast<char*>(resp.get_memory_pool.addr) + kMetaHeadroomSize);
#else
            auto pool = reinterpret_cast<MemoryPool*>(virt_base_addrs[process_id] + resp.get_memory_pool.offset + kMetaHeadroomSize);
#endif
            return pool;
        }
        return nullptr;
    }

    ControlPlaneStatus GetControlPlaneStatus() {
        return control_plane_status;
    }

    void FreeMemoryPool(MemoryPool* mempool) {
        local_rpc_request.type = RpcRequestType::kFreeMemoryPool;
#if defined (OMNIMEM_BACKEND_DPDK)
        local_rpc_request.free_memory.addr = (uint8_t*)mempool - kMetaHeadroomSize;
#else
        local_rpc_request.free_memory.offset = reinterpret_cast<uint8_t*>(mempool) - virt_base_addrs[process_id] - kMetaHeadroomSize;
#endif
        auto resp = SendLocalRpcRequest();
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to free shared memory");
    }

    int GetControlPlaneId() {
        return sock_id;
    }

#if !defined(OMNIMEM_BACKEND_DPDK)
    uint8_t** GetVirtBaseAddrs() {
        return virt_base_addrs;
    }
#endif

    void* AllocateNamedSharedForThread(const std::string& name, size_t size, uint64_t thread_id) {
        local_rpc_request.type = RpcRequestType::kGetMemory;
        if (name.length() >= kMaxNameLength) throw std::runtime_error("Name too long");
        local_rpc_request.get_memory.size = size;
        local_rpc_request.get_memory.thread_id = thread_id;
        strcpy(local_rpc_request.get_memory.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            #if defined (OMNIMEM_BACKEND_DPDK)
            auto meta = reinterpret_cast<RegionMeta*>(resp.get_memory.addr);
            #else
            auto meta = reinterpret_cast<RegionMeta*>(virt_base_addrs[process_id] + resp.get_memory.offset);
            #endif
            return reinterpret_cast<uint8_t*>(meta) + kMetaHeadroomSize;
        }
        return nullptr;
    }

    void ForkSubsystem() {
        // sock_id = control_plane_id;
        id_to_rpc_meta = std::map<int, RpcRequestMeta*>();
        sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                    std::to_string(sock_id) + ".socket";
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, sock_name.c_str());
        sock_to_control_plane = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_to_control_plane, (struct sockaddr*)&addr, sizeof(addr.sun_family) + sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane " + std::to_string(errno));

        rpc_response_receiver = new std::thread(RpcResponseReceiver);

        local_rpc_request.type = RpcRequestType::kGetProcessId;
        auto resp = SendLocalRpcRequest();
        auto old_process_id = process_id;
        if (resp.status == RpcResponseStatus::kSuccess) {
            process_id = resp.get_process_id.process_id;
            main_process_pid = resp.get_process_id.pid;
        } else {
            std::cerr << "Failed to initialize subsystem\n";
            exit(1);
        }
#if !defined(OMNIMEM_BACKEND_DPDK)
        virt_base_addrs[process_id] = virt_base_addrs[old_process_id];
#endif
    }

    int GetCurrentSocket() {
        return current_node;
    }
}