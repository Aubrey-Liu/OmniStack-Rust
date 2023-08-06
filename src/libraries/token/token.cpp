#include <omnistack/token/token.h>
#include <omnistack/memory/memory.h>
#include <filesystem>
#include <thread>
#include <mutex>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <iostream>
#include <map>

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

namespace omnistack::token
{
    struct RpcRequest {
        RpcRequestType type;
        char padding[64 - sizeof(RpcRequestType)];
        uint64_t id;
        uint64_t token_id;
        uint64_t thread_id;
    };

    struct RpcResponse {
        uint64_t id;
        RpcResponseStatus status;
    };

    struct RpcRequestMeta {
        bool cond_rpc_finished;
        std::condition_variable cond_rpc_changed;
        std::mutex cond_rpc_lock;
        RpcResponse resp;
    };

    static std::thread* rpc_response_receiver;
    static std::map<int, RpcRequestMeta*> id_to_rpc_meta;
    static thread_local RpcRequestMeta local_rpc_meta{};
    static std::mutex rpc_request_lock;
    static int rpc_id;

    static int system_id;
    static int control_plane_sock;
    static int sock_to_control_plane;

    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;
    static ControlPlaneStatus control_plane_status = ControlPlaneStatus::kStopped;

    static void ControlPlane() {
        {
            std::unique_lock _(control_plane_state_lock);
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
            EV_SET(&ev, control_plane_sock, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void *) (intptr_t) control_plane_sock);
            {
                auto ret = kevent(epfd, &ev, 1, nullptr, 0, nullptr);
                if (ret)
                    throw std::runtime_error("Failed to set kevent");
            }
#else
            epfd = epoll_create(128);
            struct epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = control_plane_sock;
            {
                auto ret = epoll_ctl(epfd, EPOLL_CTL_ADD, control_plane_sock, &ev);
                if (ret)
                    throw std::runtime_error("Failed to set epoll " + std::to_string(errno));
            }
#endif
        } catch(std::runtime_error& err) {
            std::cerr << err.what() << std::endl;
            return ;
        }
        control_plane_status = ControlPlaneStatus::kRunning;

        try {
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
                    if (fd == control_plane_sock) {
                    } else {

                    }
                }
            }
        } catch (std::runtime_error& err_info) {
            control_plane_status = ControlPlaneStatus::kStopped;
            throw err_info;
        }
        control_plane_status = ControlPlaneStatus::kStopped;
    }

    void StartControlPlane() {
        std::unique_lock _(control_plane_state_lock);
        if (control_plane_started)
            throw std::runtime_error("There is multiple control plane threads");
        control_plane_status = ControlPlaneStatus::kStarting;

        std::string control_plane_sock_name = "";
        system_id = memory::GetControlPlaneId();
        control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
            std::to_string(system_id) + ".socket";
        
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (control_plane_sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, control_plane_sock_name.c_str());
        control_plane_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (control_plane_sock < 0)
            throw std::runtime_error("Failed to create unix socket");
        std::filesystem::remove(control_plane_sock_name);
        if (bind(control_plane_sock, (struct sockaddr*)&addr,
                    control_plane_sock_name.length() + sizeof(addr.sun_family))) {
            throw std::runtime_error("Failed to bind unix socket " + control_plane_sock_name);
        }
        if (listen(control_plane_sock, 16)) {
            throw std::runtime_error("Failed to listen unix socket");
        }
        {
            auto flags = fcntl(control_plane_sock, F_GETFL);
            if (flags == -1)
                throw std::runtime_error("Faileld to get unix socket flags");
            auto ret = fcntl(control_plane_sock, F_SETFL, (flags | O_NONBLOCK));
            if (ret == -1)
                throw std::runtime_error("Failed to set unix socket flags");
        }

        {
            control_plane_thread = new std::thread(ControlPlane);
            cond_control_plane_started.wait(_, [&](){
                return !control_plane_started;
            });
        }
    }

    void RpcResponseReceiver() {
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

    static int control_plane_id;

    void InitializeSubsystem() {
        control_plane_id = memory::GetControlPlaneId();
        auto control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
            std::to_string(control_plane_id) + ".socket";
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (control_plane_sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, control_plane_sock_name.c_str());
        sock_to_control_plane = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_to_control_plane, (struct sockaddr*)&addr, sizeof(addr.sun_family) + control_plane_sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane");

        rpc_response_receiver = new std::thread(RpcResponseReceiver);
    }

    void SendTokenMessage(RpcRequestType type, Token* token) {
        local_rpc_meta.cond_rpc_finished = false;
        static RpcRequest local_req = {
            .type = type,
            .token_id = token->token_id,
            .thread_id = memory::thread_id
        };

        {
            std::unique_lock<std::mutex> _(rpc_request_lock);
            local_req.id = ++rpc_id;
            while (id_to_rpc_meta.count(local_req.id))
                local_req.id = ++rpc_id;
            id_to_rpc_meta[local_req.id] = &local_rpc_meta;
            local_rpc_meta.cond_rpc_finished = false;
            writeAll(sock_to_control_plane, reinterpret_cast<char*>(&local_req), sizeof(RpcRequest));
        }

        {
            std::unique_lock<std::mutex> _(local_rpc_meta.cond_rpc_lock);
            local_rpc_meta.cond_rpc_changed.wait(_, [](){
                return local_rpc_meta.cond_rpc_finished;
            });
        }

        if (local_rpc_meta.resp.status != RpcResponseStatus::kSuccess) {
            throw std::runtime_error("Rpc execution failed");
        }
    }
} // namespace omnistack::token
