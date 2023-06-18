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

#if defined(__APPLE__)
#include <sys/event.h>
#else
#error Not supported System OS
#endif

namespace omnistack::mem {
    enum class RPCRequestType {
        kGetProcessId = 0,
        kDestroyProcess,
        kNewThread,
        kDestroyThread
    };

    enum class RPCResponseStatus {
        kSuccess = 0,
        kUnknownProcess,
        kUnknownType,
    };

    struct RPCResponse {
        int id;
        RPCResponseStatus status;
        union {
            struct {
                uint64_t process_id;
            } get_process_id;
        };
    };

    struct RPCRequestMeta {
        bool cond_rpc_finished;
        std::condition_variable cond_rpc_changed;
        std::mutex cond_rpc_lock;
        RPCResponse resp;
    };

    struct RPCRequest {
        RPCRequestType type;
        int id;
    };

    uint64_t process_id = ~0;
    thread_local uint64_t thread_id = ~0;

    void Free(void* ptr) {
        auto meta = (RegionMeta*)(
                reinterpret_cast<char*>(ptr) - kMetaHeadroomSize
                );
        switch (meta->type) {
            case RegionType::kLocal:
                FreeLocal(ptr);
                break;
            case RegionType::kShared:
                FreeShared(ptr);
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

    constexpr int kMaxProcess = 1024;
    constexpr int kMaxControlPlane = 8;
    constexpr int kMaxIncomingProcess = 16;
    static int sock_client = 0;
    static std::string sock_name;
    static std::string sock_lock_name;
    static int sock_lock_fd = 0;
    static int sock_id = 0;

    /** PROCESS INFORMATION **/
    struct ProcessInfo {
        uint64_t process_id;
    };
    std::map<int, ProcessInfo> fd_to_process_info;
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
            int nevents = 0;
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
                    int new_fd = 0;
                    do {
                        new_fd = accept(sock_client, nullptr, nullptr);
                        if (new_fd > 0) {
                            uint64_t new_process_id = 1;
                            while (fd_to_process_info.count(new_process_id))
                                new_process_id ++;
                            fd_to_process_info[new_fd] = ProcessInfo();
                            auto& info = fd_to_process_info[new_fd];

                            /** INIT PROCESS INFO **/
                            info.process_id = new_process_id;

                            /** SET EVENT DRIVEN **/
#if defined(__APPLE__)
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
                    RPCRequest rpc_request{};
                    bool peer_closed = false;
                    {
                        ssize_t rd_bytes;
                        {
                            ssize_t sum_rd_bytes = 0;
                            do {
                                rd_bytes = read(fd, reinterpret_cast<char *>(&rpc_request) + sum_rd_bytes,
                                                sizeof(RPCRequest) - sum_rd_bytes);
                                if (rd_bytes < 0) {
                                    usleep(1);
                                    continue;
                                }
                                sum_rd_bytes += rd_bytes;
                            } while (rd_bytes != 0 && sum_rd_bytes < sizeof(RPCRequest));
                        }
                        if (rd_bytes == 0) {
                            peer_closed = true;
                        }
                    }

                    if (!peer_closed) {
                        // A normal RPC Request
                        RPCResponse resp;
                        resp.id = rpc_request.id;
                        switch (rpc_request.type) {
                            case RPCRequestType::kGetProcessId: {
                                if (fd_to_process_info.count(fd)) {
                                    auto& info = fd_to_process_info[fd];
                                    resp.status = RPCResponseStatus::kSuccess;
                                    resp.get_process_id.process_id = info.process_id;
                                } else
                                    resp.status = RPCResponseStatus::kUnknownProcess;
                                break;
                            }
                            case RPCRequestType::kNewThread: {

                            }
                            default:
                                resp.status = RPCResponseStatus::kUnknownType;
                        }

                        ssize_t sd_bytes;
                        {
                            ssize_t sum_sd_bytes = 0;
                            do {
                                sd_bytes = write(fd, reinterpret_cast<char *>(&resp) + sum_sd_bytes,
                                                 sizeof(RPCResponse) - sum_sd_bytes);
                                if (sd_bytes < 0) {
                                    usleep(1);
                                    continue ;
                                }
                                sum_sd_bytes += sd_bytes;
                            } while(sd_bytes != 0 && sum_sd_bytes < sizeof(RPCResponse));
                        }

                        if (!sd_bytes) peer_closed = true;
                    }

                    if (peer_closed) {
                        // TODO: Close Process
                    }
                }
            }
        }
    }



#if !defined(OMNIMEM_BACKEND_DPDK)
    static uint8_t** virt_base_addrs = nullptr;
    static bool is_master_process = false;
    static std::string virt_base_addrs_name = "";
    static int virt_base_addrs_fd = 0;
#endif

    void StartControlPlane() {
        std::unique_lock<std::mutex> _(control_plane_state_lock);

#if !defined(OMNIMEM_BACKEND_DPDK)
        is_master_process = true;
        virt_base_addrs_name = "omnistack_virt_base_addrs_" + std::to_string(getpid());
        {
            auto ret = shm_open(virt_base_addrs_name.c_str(), O_RDWR);
            if (!ret) throw std::runtime_error("Failed to init virt_base_addrs for already exists");
        }
        {
            virt_base_addrs_fd = shm_open(virt_base_addrs_name.c_str(), O_RDWR | O_CREAT);
            if (virt_base_addrs_fd < 0) throw std::runtime_error("Failed to init virt_base_addrs");
        }
        {
            auto ret = ftruncate(virt_base_addrs_fd, kMaxProcess * sizeof(uint8_t*));
            if (ret) throw std::runtime_error("Failed to ftruncate virt_base_addrs");
        }
        {
            virt_base_addrs =
                    reinterpret_cast<uint8_t**>(mmap(nullptr, kMaxProcess * sizeof(uint8_t*), PROT_WRITE | PROT_READ,
                                                     MAP_SHARED, virt_base_addrs_fd, 0));
            if (!virt_base_addrs)
                throw std::runtime_error("Failed to create virt_base_addrs");
        }
#endif
        {
            for (sock_id = 0; sock_id < kMaxControlPlane; sock_id ++) {
                sock_lock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                                 std::to_string(sock_id) + ".lock";
                sock_lock_fd = open(sock_lock_name.c_str(), O_RDONLY | O_CREAT);
                if (sock_lock_fd < 0) {
                    sock_lock_fd = 0;
                    continue;
                }
                if (flock(sock_lock_fd, LOCK_EX | LOCK_NB)) {
                    close(sock_lock_fd);
                    sock_lock_fd = 0;
                    continue;
                }

                sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                            std::to_string(sock_id) + ".socket";
            }

            sockaddr_un addr;
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
                auto ret = munmap(virt_base_addrs, sizeof(kMaxProcess) * sizeof(uint8_t *));
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
    static std::map<int, RPCRequestMeta*> id_to_rpc_meta;
    static thread_local RPCRequest local_rpc_request{};
    static thread_local RPCRequestMeta local_rpc_meta{};
    static std::mutex rpc_request_lock;

    static void RPCResponseReceiver() {
        RPCResponse resp;
        while (true) {
            ssize_t recv_bytes = 0;
            ssize_t last_recv_bytes;
            do {
                last_recv_bytes = read(sock_client, reinterpret_cast<char*>(&resp) + recv_bytes, sizeof(RPCResponse) - recv_bytes);
                if (last_recv_bytes == -1) {
                    if (errno == EINTR)
                        continue;
                    else
                        throw std::runtime_error("Unknown control plane error");
                } else if (last_recv_bytes == 0)
                    throw std::runtime_error("Control plane crashed");
                recv_bytes += last_recv_bytes;
            } while(recv_bytes < sizeof(RPCResponse));

            {
                std::unique_lock<std::mutex> __(rpc_request_lock);
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

    static void SendLocalRPCRequest() {
        {
            std::unique_lock<std::mutex> _(rpc_request_lock);
            id_to_rpc_meta.at(local_rpc_request.id) = &local_rpc_meta;
            local_rpc_meta.cond_rpc_finished = false;
            ssize_t sent_bytes = write(sock_client, &local_rpc_request, sizeof(RPCRequest));
            if (sent_bytes != sizeof(RPCRequest))
                throw std::runtime_error("Failed to send request");
        }
    }

    void InitializeSubsystem(int control_plane_id) {
        sock_id = control_plane_id;
        sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                    std::to_string(sock_id) + ".socket";
        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        if (sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, sock_name.c_str());
        sock_client = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_client, (struct sockaddr*)&addr, sizeof(addr.sun_family) + sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane");

        rpc_response_receiver = new std::thread(RPCResponseReceiver);

        local_rpc_request.type = RPCRequestType::kGetProcessId;
        local_rpc_request.id = 1;
        SendLocalRPCRequest();
    }

    void DestroySubsystem() {

    }
}