#include <omnistack/token/token.h>
#include <omnistack/memory/memory.h>
#include <omnistack/common/logger.h>
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
#include <set>
#include <sys/time.h>
#include <vector>
#include <queue>

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
    static uint64_t GetCurrentTick() {
        struct timeval tv{};
        gettimeofday(&tv, nullptr);
        return tv.tv_sec * 1000000ll + tv.tv_usec;
    }

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
        struct {
            memory::Pointer<Token> token;
        } new_token;
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

#if !defined(OMNIMEM_BACKEND_DPDK)
    static uint8_t** virt_base_addrs = nullptr;
#endif

    class Peer {
    public:
        Peer(int fd) : fd_(fd) {
        }

        void WriteResponse(const RpcResponse& resp) {
            if (!closed) {
                writeAll(fd_, (const char*) &resp, sizeof(resp));
            }
        }

        void Close() {
            closed = true;
            if (!closed) {
                close(fd_);
            }
        }

        int fd_;
        bool closed = false;
    };

    static void ControlPlane() {
        {
            std::unique_lock _(control_plane_state_lock);
            control_plane_started = true;
            cond_control_plane_started.notify_all();
        }
        memory::InitializeSubsystemThread();
        OMNI_LOG(common::kInfo) << "Starting token control plane, thread_id = " << memory::thread_id << "\n";
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
        static std::set<std::pair<uint64_t, Token*>> token_tick;
        static std::map<typeof(Token::token_id), Token*> id_to_token;
        static std::set<typeof(Token::token_id)> used_token_id;
        static std::map<int, std::shared_ptr<Peer>> fd_to_peer;
        static std::map<Token*, std::queue<
            std::pair<typeof(Token::token), std::pair<
            std::shared_ptr<Peer>, typeof(RpcResponse::id)>>
        >> token_queue;

        try {
            while (!stop_control_plane) {
                uint64_t min_wait_usec = 1000000;
                uint64_t now = GetCurrentTick();
                while (!token_tick.empty() && token_tick.begin()->first <= now) {
                    auto token = token_tick.begin()->second;
                    token_tick.erase(token_tick.begin());
                    token->returning = 0;
                    token->token = 0;

                    bool assigned = false;
                    while (!assigned && token_queue[token].size()) {
                        auto frt = token_queue[token].front();
                        token_queue[token].pop();
                        if (!frt.second.first->closed) {
                            token->token = frt.first;
                            RpcResponse succ = {
                                .id = frt.second.second,
                                .status = RpcResponseStatus::kSuccess
                            };
                            
                            try {
                                frt.second.first->WriteResponse(succ);
                            } catch (std::runtime_error& err_info) {
                                token->token = 0;
                                continue;
                            }
                            assigned = true;
                        }
                    }

                    if (token_queue[token].size()) {
                        token->need_return[token->token] = true;
                        token->returning = 1;
                        token_tick.insert({now + 1000000, token});
                    }
                }
                for (auto& [tick, token] : token_tick) {
                    min_wait_usec = std::min(min_wait_usec, tick - now);
                }
                int nevents;
#if defined (__APPLE__)
                struct timespec timeout = {
                        .tv_sec = 1,
                        .tv_nsec = min_wait_usec * 1000
                };
                nevents = kevent(epfd, nullptr, 0, events, kMaxEvents, &timeout);
#else
                nevents = epoll_wait(epfd, events, kMaxEvents, min_wait_usec / 1000 + 1);
#endif
                for (int eidx = 0; eidx < nevents; eidx ++) {
                    auto& evt = events[eidx];
#if defined (__APPLE__)
                    auto fd = (int)(intptr_t)evt.udata;
#else
                    auto fd = evt.data.fd;
#endif
                    if (fd == control_plane_sock) {
                        int new_fd;
                        do {
                            new_fd = accept(control_plane_sock, nullptr, nullptr);
                            if (new_fd > 0) {
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
                                fd_to_peer[new_fd] = std::make_shared<Peer>(new_fd);
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
                        bool peer_closed = false;
                        RpcRequest request;
                        RpcResponse resp;
                        bool send_resp = true;

                        auto peer = fd_to_peer[fd];
                        if (!peer)
                            throw std::runtime_error("This should not happen");

                        try {
                            readAll(fd, reinterpret_cast<char*>(&request), sizeof(request));
                        } catch (std::runtime_error& err_info) {
                            peer_closed = true;
                        }

                        if (!peer_closed) {
                            switch (request.type) {
                                case RpcRequestType::kCreateToken: {
                                    auto target_thread_id = request.thread_id;
                                    auto token = reinterpret_cast<Token*>(memory::AllocateNamedSharedForThread("", sizeof(Token), target_thread_id));
                                    if (token == nullptr)
                                        throw std::runtime_error("Failed to create token");
                                    token->token_id = 1;
                                    while (used_token_id.count(token->token_id))
                                        token->token_id ++;
                                    used_token_id.insert(token->token_id);
                                    token->returning = 0;
                                    token->token = 0;
                                    memset(token->need_return, 0, sizeof(token->need_return));
                                    id_to_token[token->token_id] = token;

                                    resp.status = RpcResponseStatus::kSuccess;
                                    resp.new_token.token = memory::Pointer(token);
                                    break;
                                }
                                case RpcRequestType::kDestroyToken: {
                                    auto token = id_to_token[request.token_id];
                                    used_token_id.erase(token->token_id);
                                    id_to_token.erase(token->token_id);
                                    memory::FreeNamedShared(token);
                                    resp.status = RpcResponseStatus::kSuccess;
                                    break;
                                }
                                case RpcRequestType::kAcquire: {
                                    auto token = id_to_token[request.token_id];
                                    if (token->token == 0) {
                                        token->token = request.thread_id;
                                        resp.status = RpcResponseStatus::kSuccess;
                                    } else if(token->token != request.thread_id) {
                                        send_resp = false;
                                        token_queue[token].push(
                                            std::make_pair(
                                                request.thread_id,
                                                std::make_pair(peer, request.id)
                                            )
                                        );
                                        if (token->returning == 0) {
                                            token->returning = 1;
                                            token->need_return[token->token] = true;
                                            token_tick.insert({now + 1000000, token});
                                        }
                                    } else {
                                        resp.status = RpcResponseStatus::kSuccess;
                                    }
                                    break;
                                }
                                case RpcRequestType::kReturn: {
                                    resp.status = RpcResponseStatus::kSuccess;
                                    auto token = id_to_token[request.token_id];
                                    token->need_return[request.thread_id] = false;
                                    if (token->token == request.thread_id) { // Valid Return
                                        for (auto& iter : token_tick) {
                                            if (iter.second == token) {
                                                token_tick.erase(iter);
                                                break;
                                            }
                                        }
                                        token->token = 0;
                                        token->returning = 0;

                                        bool assigned = false;
                                        while (!assigned && token_queue[token].size()) {
                                            auto frt = token_queue[token].front();
                                            token_queue[token].pop();
                                            if (!frt.second.first->closed) {
                                                token->token = frt.first;
                                                RpcResponse succ = {
                                                    .id = frt.second.second,
                                                    .status = RpcResponseStatus::kSuccess
                                                };
                                                
                                                try {
                                                    frt.second.first->WriteResponse(succ);
                                                } catch (std::runtime_error& err_info) {
                                                    token->token = 0;
                                                    continue;
                                                }
                                                assigned = true;
                                            }
                                        }

                                        if (token_queue[token].size()) {
                                            token->returning = 1;
                                            token->need_return[token->token] = true;
                                            token_tick.insert({now + 1000000, token});
                                        }
                                    }
                                    break;
                                }
                            }
                            resp.id = request.id;
                            try {
                                if (send_resp) {
                                    peer->WriteResponse(resp);
                                }
                            } catch (std::runtime_error& err_info) {
                                peer_closed = true;
                            }
                        }

                        if (peer_closed) {
                            peer->Close();
                            fd_to_peer.erase(fd);
                        }
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
        control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_token_sock" +
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
        auto control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_token_sock" +
            std::to_string(control_plane_id) + ".socket";

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (control_plane_sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, control_plane_sock_name.c_str());
        sock_to_control_plane = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_to_control_plane, (struct sockaddr*)&addr, sizeof(addr.sun_family) + control_plane_sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane " + std::to_string(errno));

        rpc_response_receiver = new std::thread(RpcResponseReceiver);
    }

    void SendTokenMessage(RpcRequestType type, Token* token, uint64_t thread_id) {
        local_rpc_meta.cond_rpc_finished = false;
        static thread_local RpcRequest local_req;
        local_req.type = type;
        local_req.token_id = token ? token->token_id : ~0;
        local_req.thread_id = thread_id;

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

    ControlPlaneStatus GetControlPlaneStatus() {
        return control_plane_status;
    }

    Token* CreateToken() {
        SendTokenMessage(RpcRequestType::kCreateToken, nullptr, memory::thread_id);
        auto& resp = local_rpc_meta.resp;
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to create token");
        return resp.new_token.token.Get();
    }

    Token* CreateTokenForThread(uint64_t thread_id) {
        SendTokenMessage(RpcRequestType::kCreateToken, nullptr, thread_id);
        auto& resp = local_rpc_meta.resp;
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to create token");
        return resp.new_token.token.Get();
    }

    void DestroyToken(Token* token) {
        SendTokenMessage(RpcRequestType::kDestroyToken, token, memory::thread_id);
        auto& resp = local_rpc_meta.resp;
        if (resp.status != RpcResponseStatus::kSuccess)
            throw std::runtime_error("Failed to destroy token");
    }

    void ForkSubsystem() {
        control_plane_id = memory::GetControlPlaneId();
        auto control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_token_sock" +
            std::to_string(control_plane_id) + ".socket";

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (control_plane_sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, control_plane_sock_name.c_str());
        sock_to_control_plane = socket(AF_UNIX, SOCK_STREAM, 0);

        if (connect(sock_to_control_plane, (struct sockaddr*)&addr, sizeof(addr.sun_family) + control_plane_sock_name.length()))
            throw std::runtime_error("Failed to connect to control plane " + std::to_string(errno));

        id_to_rpc_meta = std::map<int, RpcRequestMeta*>();
        rpc_response_receiver = new std::thread(RpcResponseReceiver);
    }
} // namespace omnistack::token
