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
#error Not supported System OS
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
                throw std::runtime_error("read error");
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
        kFreeMemoryPool
    };

    enum class RpcResponseStatus {
        kSuccess = 0,
        kUnknownProcess,
        kUnknownType,
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
                uint64_t offset;
            } get_memory;
            struct {
                uint64_t offset;
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
            } get_memory;
            struct {
                size_t chunk_size;
                size_t chunk_count;
                char name[kMaxNameLength];
            } get_memory_pool;
            struct {
                uint64_t offset;
            } free_memory;
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
#endif
        } catch(std::runtime_error& err) {
            std::cerr << err.what() << std::endl;
            return ;
        }

        while (!stop_control_plane) {
            int nevents;
#if defined(__APPLE__)
            struct timespec timeout = {
                    .tv_sec = 1,
                    .tv_nsec = 0
            };
            nevents = kevent(epfd, nullptr, 0, events, kMaxEvents, &timeout);
#endif
            for (int eidx = 0; eidx < nevents; eidx ++) {
                auto& evt = events[eidx];
#if defined(__APPLE__)
                auto fd = (int)(intptr_t)evt.udata;
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
                                resp.new_thread.thread_id = 0;
                                resp.status = RpcResponseStatus::kSuccess;
                                break;
                            }
                            case RpcRequestType::kGetMemory: {
                                auto region_name = std::string(rpc_request.get_memory.name);
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
                                    auto local_meta = reinterpret_cast<RegionMeta *>(rte_malloc(aligned_size));
#endif
                                    local_meta->size = aligned_size;
                                    local_meta->type = RegionType::kNamedShared;
                                    auto &process_info = fd_to_process_info[fd];
                                    local_meta->process_id = process_info.process_id;
                                    local_meta->iova = 0;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    local_meta->offset = region_info.second;
#else
                                    local_meta->offset = 0;
#endif
                                    local_meta->ref_cnt = 1;

#if !defined(OMNIMEM_BACKEND_DPDK)
                                    region_info.first -= aligned_size;
                                    region_info.second += aligned_size;
                                    if (region_info.first > 0)
                                        usable_region.insert(region_info);
#endif
                                    used_regions.insert(local_meta);
                                    resp.get_memory.offset = local_meta->offset;
                                    resp.status = RpcResponseStatus::kSuccess;
                                    if (region_name != "")
                                        region_name_to_meta[region_name] = local_meta;
                                } else {
                                    auto &meta = region_name_to_meta[region_name];
                                    meta->ref_cnt ++;
                                    resp.get_memory.offset = meta->offset;
                                    resp.status = RpcResponseStatus::kSuccess;
                                }
                                break;
                            }
                            case RpcRequestType::kGetMemoryPool: {
                                auto pool_name = std::string(rpc_request.get_memory_pool.name);
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
                                    mempool_meta = reinterpret_cast<MemoryPool *>(rte_malloc(aligned_mempool_size));
#endif
                                    mempool_meta->size = aligned_mempool_size;
                                    mempool_meta->type = RegionType::kNamedShared;
                                    mempool_meta->process_id = 0;
                                    mempool_meta->iova = 0;
#if !defined(OMNIMEM_BACKEND_DPDK)
                                    mempool_meta->offset = reinterpret_cast<uint8_t*>(mempool_meta) - virt_shared_region;
#else
                                    mempool_meta->offset = 0;
#endif
                                    mempool_meta->ref_cnt = 1;
                                    resp.get_memory_pool.offset = mempool_meta->offset;
                                    resp.status = RpcResponseStatus::kSuccess;

                                    auto mempool = reinterpret_cast<MemoryPool*>(reinterpret_cast<uint8_t*>(mempool_meta) + kMetaHeadroomSize);

                                    mempool->chunk_size_ = rpc_request.get_memory_pool.chunk_size;
                                    mempool->chunk_count_ = rpc_request.get_memory_pool.chunk_count;
                                    pthread_mutexattr_t init_attr;
                                    pthread_mutexattr_init(&init_attr);
                                    pthread_mutexattr_setpshared(&init_attr, PTHREAD_PROCESS_SHARED);
                                    pthread_mutex_init(&mempool->recycle_mutex_, &init_attr);
                                    pthread_mutexattr_destroy(&init_attr);

                                    { // Allocate Chunks
                                    }
                                    { // Allocate & Set Blocks
                                    }
                                } else {
                                    auto &meta = pool_name_to_meta[pool_name];
                                    meta->ref_cnt ++;
                                    resp.get_memory_pool.offset = meta->offset;
                                    resp.status = RpcResponseStatus::kSuccess;
                                }
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
    }

    void StartControlPlane() {
        std::unique_lock<std::mutex> _(control_plane_state_lock);

#if !defined(OMNIMEM_BACKEND_DPDK)
        {
            virt_base_addrs_name = "omnistack_virt_base_addrs_" + std::to_string(getpid());
            {
                auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
                if (ret >= 0) throw std::runtime_error("Failed to init virt_base_addrs for already exists");
            }
            {
                virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR | O_CREAT);
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
                auto ret = shm_open(virt_shared_region_name.c_str(), O_RDWR);
                if (ret >= 0) throw std::runtime_error("Failed to init virt_shared_region for already exists");
            }
            {
                virt_shared_region_control_plane_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR | O_CREAT);
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
                sock_lock_fd = open(sock_lock_name.c_str(), O_RDONLY | O_CREAT);
                if (sock_lock_fd < 0)
                    continue;
                if (flock(sock_lock_fd, LOCK_EX | LOCK_NB)) {
                    close(sock_lock_fd);
                    continue;
                }

                sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                            std::to_string(sock_id) + ".socket";
            }

            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            if (sock_name.length() >= sizeof(addr.sun_path))
                throw std::runtime_error("Failed to assign sock path to unix domain addr");
            strcpy(addr.sun_path, sock_name.c_str());
            sock_client = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock_client < 0)
                throw std::runtime_error("Failed to create unix socket");
            if (bind(sock_client, (struct sockaddr*)&addr,
                     sock_name.length() + sizeof(addr.sun_family))) {
                throw std::runtime_error("Failed to bind unix socket");
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

    void InitializeSubsystem(int control_plane_id) {
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
                    virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
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
                    virt_shared_region_fd = shm_open(virt_shared_region_name.c_str(), O_RDWR);
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
        strcpy(local_rpc_request.get_memory.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            auto meta = reinterpret_cast<RegionMeta*>(virt_base_addrs[process_id] + resp.get_memory.offset);
            return reinterpret_cast<uint8_t*>(meta) + kMetaHeadroomSize;
        }
        return nullptr;
    }

    void FreeNamedShared(void* ptr) {
        local_rpc_request.type = RpcRequestType::kFreeMemory;
        local_rpc_request.free_memory.offset = reinterpret_cast<uint8_t*>(ptr) - virt_base_addrs[process_id] - kMetaHeadroomSize;
        auto resp = SendLocalRpcRequest();
    }

    void MemoryPool::Put(void* ptr) {
        auto real_ptr = reinterpret_cast<uint8_t*>(ptr) - kMetaHeadroomSize;
        auto cache = local_free_cache_[thread_id];
        if (cache->used == kMemoryPoolLocalCache) { // Get a empty block from main pool
            cache->cnt = cache->used;
            pthread_mutex_lock(&recycle_mutex_);
            cache->next = full_container_offset_;
            full_container_offset_ = reinterpret_cast<uint8_t*>(cache) - virt_base_addrs[process_id];

            auto container_ptr = reinterpret_cast<MemoryPoolBatch*>(virt_base_addrs[process_id] + empty_container_offset_);
            empty_container_offset_ = container_ptr->next;
            pthread_mutex_unlock(&recycle_mutex_);

            local_free_cache_[thread_id] = cache = container_ptr;
            container_ptr->used = 0;
        }
        cache->offsets[cache->used ++] = (real_ptr - virt_base_addrs[process_id]);
    }

    void* MemoryPool::Get() {
        if (local_cache_[thread_id]->used == local_cache_[thread_id]->cnt) { // chunk ran out
            pthread_mutex_lock(&recycle_mutex_);
            auto container_ptr = reinterpret_cast<MemoryPoolBatch*>(virt_base_addrs[process_id] + full_container_offset_);
            full_container_offset_ = container_ptr->next;

            local_cache_[thread_id]->next = empty_container_offset_;
            empty_container_offset_ = reinterpret_cast<uint8_t*>(local_cache_[thread_id]) - virt_base_addrs[process_id];

            pthread_mutex_unlock(&recycle_mutex_);
            /// TODO: prefetch

            local_cache_[thread_id] = container_ptr;
        }
        auto ret_offset = local_cache_[thread_id]->offsets[local_cache_[thread_id]->used ++];
        auto ret = virt_base_addrs[process_id] + ret_offset;
        auto meta = reinterpret_cast<RegionMeta*>(ret);
        meta->mempool = this;
        meta->offset = ret_offset;
        meta->size = chunk_size_;
        meta->process_id = process_id;
        meta->type = RegionType::kMempoolChunk;
        return ret + kMetaHeadroomSize;
    }

    MemoryPool* AllocateMemoryPool(const std::string& name, size_t chunk_size, size_t chunk_count) {
        local_rpc_request.type = RpcRequestType::kGetMemoryPool;
        if (name.length() >= kMaxNameLength) throw std::runtime_error("Name too long");
        local_rpc_request.get_memory_pool.chunk_size = chunk_size;
        local_rpc_request.get_memory_pool.chunk_count = chunk_count;
        strcpy(local_rpc_request.get_memory_pool.name, name.c_str());
        auto resp = SendLocalRpcRequest();
        if (resp.status == RpcResponseStatus::kSuccess) {
            auto pool = reinterpret_cast<MemoryPool*>(virt_base_addrs[process_id] + resp.get_memory_pool.offset + kMetaHeadroomSize);
            return pool;
        }
        return nullptr;
    }
}