//
// Created by Jeremy Guo on 2023/6/17.
//

#include <omnistack/memory/memory.h>
#include <thread>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/un.h>
#include <map>
#include <filesystem>
#include <sys/socket.h>
#include <iostream>
#include <set>

#if defined(__APPLE__)
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

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_eal.h>
#include <rte_malloc.h>
#include <numa.h>
#endif

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
                #if defined(OMNIMEM_BACKEND_DPDK)
                void* addr;
                #else
                uint64_t offset;
                #endif
            } get_memory;
            struct {
                #if defined(OMNIMEM_BACKEND_DPDK)
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
#if defined(OMNIMEM_BACKEND_DPDK)
                void* addr;
#else
                uint64_t offset;
#endif
            } free_memory;
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

    void MemoryPool::PutBack(void *ptr) {
        auto meta = (RegionMeta*)( reinterpret_cast<char*>(ptr) - kMetaHeadroomSize );
        if (meta->type == RegionType::kMempoolChunk) {
            if (meta->mempool != nullptr)
                meta->mempool->Put(ptr);
            else throw std::runtime_error("Chunk's mem-pool is nullptr");
        } else throw std::runtime_error("Ptr is not a chunk");
    }

    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;

    static int sock_client = 0;
    static std::string sock_name;
    static std::string sock_lock_name;
    static int sock_lock_fd = 0;
    static int sock_id = 0;

    static ControlPlaneStatus control_plane_status = ControlPlaneStatus::kStopped;

#if !defined(OMNIMEM_BACKEND_DPDK)
    static uint8_t** virt_base_addrs = nullptr;
    static std::string virt_base_addrs_name;
    static int virt_base_addrs_fd = 0;

    static std::string virt_shared_region_name;
    static int virt_shared_region_fd = 0;

    static int virt_shared_region_control_plane_fd = 0;
    static uint8_t* virt_shared_region = nullptr;

    std::set<std::pair<uint64_t, uint64_t> > usable_region;
#endif
    std::set<RegionMeta*> used_regions;
    std::map<std::string, RegionMeta*> region_name_to_meta;
    std::map<std::string, RegionMeta*> pool_name_to_meta;

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

    void ControlPlane() {
        {
            std::unique_lock<std::mutex> _(control_plane_state_lock);
            control_plane_started = true;
            cond_control_plane_started.notify_all();
        }

        int epfd;
        constexpr int kMaxEvents = 16;
#if defined(__APPLE__)
        struct kevent events[kMaxEvents];
#else
        struct epoll_event events[kMaxEvents];
#endif
        try {
#if defined(__APPLE__)
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
#if defined(__APPLE__)
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
#if defined(__APPLE__)
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
#if defined(__APPLE__)
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
                        resp.id = rpc_request.id;
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
                                    auto aligned_size =
                                            (rpc_request.get_memory.size + kMetaHeadroomSize + 63) / 64 * 64;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    auto usable_region_iter = usable_region.lower_bound(
                                            std::make_pair(aligned_size, 0));
                                    if (usable_region_iter == usable_region.end())
                                        throw std::runtime_error("No usable region");
                                    auto region_info = *usable_region_iter;
                                    usable_region.erase(usable_region_iter);
                                    auto local_meta = reinterpret_cast<RegionMeta *>(virt_shared_region +
                                                                                     region_info.second);
#else
                                    auto local_meta = reinterpret_cast<RegionMeta *>(
                                        rte_malloc_socket(nullptr, aligned_size, 64, (thread_cpu == -1 ? -1 : numa_node_of_cpu(
                                            thread_cpu
                                        )))
                                    );
#endif
                                    local_meta->size = aligned_size;
                                    local_meta->type = RegionType::kNamedShared;
                                    auto &process_info = fd_to_process_info[fd];
                                    local_meta->process_id = process_info.process_id;
                                    local_meta->iova = 0;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    local_meta->offset = region_info.second;
#else
                                    local_meta->addr = local_meta;
#endif
                                    local_meta->ref_cnt = 1;

#if !defined(OMNIMEM_BACKEND_DPDK)
                                    region_info.first -= aligned_size;
                                    region_info.second += aligned_size;
                                    if (region_info.first > 0)
                                        usable_region.insert(region_info);
#endif
                                    used_regions.insert(local_meta);
                                    resp.get_memory.addr = local_meta->addr;
                                    resp.status = RpcResponseStatus::kSuccess;
                                    if (region_name != "")
                                        region_name_to_meta[region_name] = local_meta;
                                } else {
                                    auto &meta = region_name_to_meta[region_name];
                                    meta->ref_cnt ++;
                                    resp.get_memory.addr = meta->addr;
                                    resp.status = RpcResponseStatus::kSuccess;
                                }
                                break;
                            }
                            case RpcRequestType::kGetMemoryPool: {
                                auto pool_name = std::string(rpc_request.get_memory_pool.name);
                                auto thread_cpu = thread_id_to_cpu[rpc_request.get_memory_pool.thread_id];
                                if (pool_name == "" || pool_name_to_meta.count(pool_name) == 0) {
                                    auto aligned_mempool_size = (sizeof(MemoryPool) + kMetaHeadroomSize + 63) / 64 * 64;
                                    RegionMeta* mempool_meta = nullptr;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    {
                                        auto usable_iter = usable_region.lower_bound(
                                                std::make_pair(aligned_mempool_size, 0));
                                        if (usable_iter == usable_region.end())
                                            throw std::runtime_error("No usable region for mempool");
                                        auto region_info = *usable_iter;
                                        usable_region.erase(usable_iter);
                                        mempool_meta = reinterpret_cast<RegionMeta *>(virt_shared_region +
                                                                                       region_info.second);
                                        region_info.first -= aligned_mempool_size;
                                        region_info.second += aligned_mempool_size;
                                        if (region_info.first > 0)
                                            usable_region.insert(region_info);
                                    }
#else
                                    mempool_meta = reinterpret_cast<RegionMeta *>(
                                        rte_malloc_socket(nullptr, aligned_mempool_size, 64, (thread_cpu == -1 ? -1 : numa_node_of_cpu(
                                            thread_cpu
                                        )))
                                    );
#endif
                                    mempool_meta->size = aligned_mempool_size;
                                    mempool_meta->type = RegionType::kNamedShared;
                                    mempool_meta->process_id = 0;
                                    mempool_meta->iova = 0;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    mempool_meta->offset = reinterpret_cast<uint8_t*>(mempool_meta) - virt_shared_region;
#else
                                    mempool_meta->addr = mempool_meta;
#endif
                                    mempool_meta->ref_cnt = 1;
                                    resp.get_memory_pool.addr = mempool_meta->addr;
                                    resp.status = RpcResponseStatus::kSuccess;

                                    auto mempool = reinterpret_cast<MemoryPool*>(reinterpret_cast<uint8_t*>(mempool_meta) + kMetaHeadroomSize);

                                    mempool->chunk_size_ = (rpc_request.get_memory_pool.chunk_size + 63 + kMetaHeadroomSize) / 64 * 64;
                                    mempool->chunk_count_ = (rpc_request.get_memory_pool.chunk_count + 63) / 64 * 64;
                                    pthread_mutexattr_t init_attr;
                                    pthread_mutexattr_init(&init_attr);
                                    pthread_mutexattr_setpshared(&init_attr, PTHREAD_PROCESS_SHARED);
                                    pthread_mutex_init(&mempool->recycle_mutex_, &init_attr);
                                    pthread_mutexattr_destroy(&init_attr);


                                    { // Allocate Chunks
                                        auto aligned_chunk_region_size = mempool->chunk_size_ * mempool->chunk_count_;
                                        auto need_allocate_chunk_region_size = (aligned_chunk_region_size + kMetaHeadroomSize + 63) / 64 * 64;
                                        auto thread_cpu = thread_id_to_cpu[rpc_request.get_memory_pool.thread_id];
#if !defined(OMNIMEM_BACKEND_DPDK)
                                        {
                                            auto usable_iter = usable_region.lower_bound(
                                                    std::make_pair(need_allocate_chunk_region_size, 0));
                                            if (usable_iter == usable_region.end())
                                                throw std::runtime_error("No usable region for mempool chunks");
                                            auto region_info = *usable_iter;
                                            usable_region.erase(usable_iter);
                                            mempool->region_offset_ = region_info.second;
                                            region_info.first -= need_allocate_chunk_region_size;
                                            region_info.second += need_allocate_chunk_region_size;
                                            if (region_info.first > 0)
                                                usable_region.insert(region_info);
                                        }
#else
                                        mempool->region_ptr_ = (uint8_t*)rte_malloc_socket(nullptr, need_allocate_chunk_region_size, 64, (thread_cpu == -1 ? -1 : numa_node_of_cpu(
                                            thread_cpu
                                        )));
#endif
                                    }
                                    { // Allocate & Set Blocks
                                        auto block_need_allocate = (mempool->chunk_count_ + kMemoryPoolLocalCache) / kMemoryPoolLocalCache + 2 * kMaxThread + 1;
#if defined(OMNIMEM_BACKEND_DPDK)
                                        auto chunk_region_begin = mempool->region_ptr_;
                                        mempool->batch_block_ptr_ = (uint8_t*)rte_malloc_socket(nullptr, block_need_allocate * sizeof(MemoryPoolBatch), 64, (thread_cpu == -1 ? -1 : numa_node_of_cpu(
                                            thread_cpu
                                        )));
                                        mempool->full_block_ptr_ = nullptr;
                                        mempool->empty_block_ptr_ = nullptr;
                                        auto current_block = reinterpret_cast<MemoryPoolBatch*>(mempool->batch_block_ptr_);
                                        int used_chunk = 0;

                                        for (int i = 0; i < mempool->chunk_count_; i += kMemoryPoolLocalCache, used_chunk ++) {
                                            current_block->cnt = std::min(mempool->chunk_count_ - i, kMemoryPoolLocalCache);
                                            current_block->used = 0;
                                            for (int j = 0; j < current_block->cnt; j ++) {
                                                auto chunk = chunk_region_begin + (i + j) * mempool->chunk_size_;
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
                                        {
                                            auto usable_iter = usable_region.lower_bound(
                                                    std::make_pair(block_need_allocate * sizeof(MemoryPoolBatch), 0));
                                            if (usable_iter == usable_region.end())
                                                throw std::runtime_error("No usable region for mempool chunks");
                                            auto region_info = *usable_iter;
                                            usable_region.erase(usable_iter);
                                            mempool->batch_block_offset_ = region_info.second;
                                            region_info.first -= block_need_allocate * sizeof(MemoryPoolBatch);
                                            region_info.second += block_need_allocate * sizeof(MemoryPoolBatch);
                                            if (region_info.first > 0)
                                                usable_region.insert(region_info);
                                        }

                                        mempool->full_block_offset_ = ~0;
                                        mempool->empty_block_offset_ = ~0;
                                        auto current_block = reinterpret_cast<MemoryPoolBatch*>(virt_shared_region + mempool->batch_block_offset_);
                                        int used_chunk = 0;
                                        for (int i = 0; i < mempool->chunk_count_; i += kMemoryPoolLocalCache, used_chunk ++) {
                                            current_block->cnt = std::min(mempool->chunk_count_ - i, kMemoryPoolLocalCache);
                                            current_block->used = 0;
                                            for (int j = 0; j < current_block->cnt; j ++) {
                                                auto chunk = chunk_region_begin + (i + j) * mempool->chunk_size_;
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

                                    pool_name_to_meta[pool_name] = mempool_meta;
                                } else {
                                    auto &meta = pool_name_to_meta[pool_name];
                                    meta->ref_cnt ++;
                                    resp.get_memory_pool.addr = meta->addr;
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
                            default:
                                resp.status = RpcResponseStatus::kUnknownType;
                        }

                        try {
                            writeAll(fd, reinterpret_cast<char *>(&resp), sizeof(RpcResponse));
                        } catch(std::runtime_error& err_info) {
                            peer_closed = true;
                        }
                    }

                    if (peer_closed) {
                        // TODO: Close Process
                    }
                }
            }
        }

        control_plane_status = ControlPlaneStatus::kStopped;
    }

    void StartControlPlane(
#if defined(OMNIMEM_BACKEND_DPDK)
        bool init_dpdk
#endif
    ) {
        control_plane_status = ControlPlaneStatus::kStarting;
        std::unique_lock<std::mutex> _(control_plane_state_lock);
#if defined(OMNIMEM_BACKEND_DPDK)
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
                #if defined(__APPLE__)
                auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
                #else
                auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR, 0666);
                #endif
                if (ret >= 0) throw std::runtime_error("Failed to init virt_base_addrs for already exists");
            }
            {
                #if defined(__APPLE__)
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
                #if defined(__APPLE__)
                auto ret = shm_open(virt_shared_region_name.c_str(), O_RDWR);
                #else
                auto ret = shm_open(virt_shared_region_name.c_str(), O_RDWR, 0666);
                #endif
                if (ret >= 0) throw std::runtime_error("Failed to init virt_shared_region for already exists");
            }
            {
                #if defined(__APPLE__)
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
                sock_lock_fd = open(sock_lock_name.c_str(), O_WRONLY | O_CREAT);
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
            readAll(sock_client, reinterpret_cast<char*>(&resp), sizeof(RpcResponse));
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
            writeAll(sock_client, reinterpret_cast<char*>(&local_rpc_request), sizeof(RpcRequest));
        }

        {
            std::unique_lock<std::mutex> _(local_rpc_meta.cond_rpc_lock);
            local_rpc_meta.cond_rpc_changed.wait(_, [](){
                return local_rpc_meta.cond_rpc_finished;
            });
        }

        return local_rpc_meta.resp;
    }

    void InitializeSubsystem(int control_plane_id,
#if defined(OMNIMEM_BACKEND_DPDK)
            bool init_dpdk
#endif
        ) {
#if defined(OMNIMEM_BACKEND_DPDK)
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
        rpc_id = 0;
        sock_id = control_plane_id;
        sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                    std::to_string(sock_id) + ".socket";
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, sock_name.c_str());
        sock_client = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_client, (struct sockaddr*)&addr, sizeof(addr.sun_family) + sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane");

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
                    #if defined(__APPLE__)
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
                    #if defined(__APPLE__)
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
            #if defined(OMNIMEM_BACKEND_DPDK)
            auto meta = reinterpret_cast<RegionMeta*>(resp.get_memory.addr);
            #else
            auto meta = reinterpret_cast<RegionMeta*>(virt_base_addrs[process_id] + resp.get_memory.offset);
            #endif
            return reinterpret_cast<uint8_t*>(meta) + kMetaHeadroomSize;
        }
        return nullptr;
    }

    void FreeNamedShared(void* ptr) {
        local_rpc_request.type = RpcRequestType::kFreeMemory;
#if defined(OMNIMEM_BACKEND_DPDK)
        local_rpc_request.free_memory.addr = (uint8_t*)ptr - kMetaHeadroomSize;
#else
        local_rpc_request.free_memory.offset = reinterpret_cast<uint8_t*>(ptr) - virt_base_addrs[process_id] - kMetaHeadroomSize;
#endif
        auto resp = SendLocalRpcRequest();
    }

    void MemoryPool::Put(void* ptr) {
        auto real_ptr = reinterpret_cast<uint8_t*>(ptr) - kMetaHeadroomSize;
        auto cache = local_free_cache_[thread_id];
        if (!cache) [[unlikely]] {
            pthread_mutex_lock(&recycle_mutex_);
#if defined(OMNIMEM_BACKEND_DPDK)
            auto container_ptr = empty_block_ptr_;
            empty_block_ptr_ = container_ptr->next;
#else
            auto container_ptr = reinterpret_cast<MemoryPoolBatch *>(virt_base_addrs[process_id] +
                                                                     empty_block_offset_);
            empty_block_offset_ = container_ptr->next;
#endif
            pthread_mutex_unlock(&recycle_mutex_);

            local_free_cache_[thread_id] = cache = container_ptr;
            container_ptr->used = 0;
        } else if (cache->used == kMemoryPoolLocalCache) [[unlikely]] { // Get a empty block from main pool
            cache->cnt = cache->used;
            pthread_mutex_lock(&recycle_mutex_);
#if defined(OMNIMEM_BACKEND_DPDK)
            cache->next = full_block_ptr_;
            full_block_ptr_ = cache;
#else
            cache->next = full_block_offset_;
            full_block_offset_ = reinterpret_cast<uint8_t*>(cache) - virt_base_addrs[process_id];
#endif

#if defined(OMNIMEM_BACKEND_DPDK)
            auto container_ptr = empty_block_ptr_;
            empty_block_ptr_ = container_ptr->next;
#else
            auto container_ptr = reinterpret_cast<MemoryPoolBatch*>(virt_base_addrs[process_id] + empty_block_offset_);
            empty_block_offset_ = container_ptr->next;
#endif
            pthread_mutex_unlock(&recycle_mutex_);

            local_free_cache_[thread_id] = cache = container_ptr;
            container_ptr->used = 0;
        }
        if (cache) [[likely]] {
#if defined(OMNIMEM_BACKEND_DPDK)
            cache->addrs[cache->used++] = real_ptr;
#else
            cache->offsets[cache->used++] = (real_ptr - virt_base_addrs[process_id]);
#endif
        } else throw std::runtime_error("Cache is nullptr");
    }

    void* MemoryPool::Get() {
        if (local_cache_[thread_id] == nullptr) [[unlikely]] {
            pthread_mutex_lock(&recycle_mutex_);
#if defined(OMNIMEM_BACKEND_DPDK)
            if (full_block_ptr_ != nullptr) {
                auto container_ptr = full_block_ptr_;
                full_block_ptr_ = container_ptr->next;

                /// TODO: prefetch

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
#if defined(OMNIMEM_BACKEND_DPDK)
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
#if defined(OMNIMEM_BACKEND_DPDK)
            auto ret = local_cache_[thread_id]->addrs[local_cache_[thread_id]->used++];
#else
            auto ret_offset = local_cache_[thread_id]->offsets[local_cache_[thread_id]->used++];
            auto ret = virt_base_addrs[process_id] + ret_offset;
#endif
            auto meta = reinterpret_cast<RegionMeta *>(ret);
            meta->mempool = this;
            meta->addr = ret;
            meta->size = chunk_size_;
            meta->process_id = process_id;
            meta->type = RegionType::kMempoolChunk;
            return (char*)ret + kMetaHeadroomSize;
        }
        return nullptr;
    }

    MemoryPool* AllocateMemoryPool(const std::string& name, size_t chunk_size, size_t chunk_count) {
        local_rpc_request.type = RpcRequestType::kGetMemoryPool;
        if (name.length() >= kMaxNameLength) throw std::runtime_error("Name too long");
        local_rpc_request.get_memory_pool.chunk_size = chunk_size;
        local_rpc_request.get_memory_pool.chunk_count = chunk_count;
        strcpy(local_rpc_request.get_memory_pool.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
#if defined(OMNIMEM_BACKEND_DPDK)
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
}