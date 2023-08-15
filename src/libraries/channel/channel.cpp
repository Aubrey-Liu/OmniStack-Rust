#include <omnistack/channel/channel.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
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

namespace omnistack::channel {
    enum class RpcRequestType {
        kGetChannel = 0,
        kDestroyChannel,
        kGetMultiWriterChannel,
        kDestroyMultiWriterChannel,
        kGetRawChannel,
    };

    struct RpcRequest {
        uint64_t id;
        RpcRequestType type;
        union  {
            struct {
                char name[128];
                uint64_t thread_id;
            } get_channel;
            struct {
                char name[128];
                uint64_t thread_id;
            } get_raw_channel;
        };
    };

    enum class RpcResponseStatus {
        kSuccess = 0,
        kFail
    };

    struct RpcResponse {
        uint64_t id;
        RpcResponseStatus status;
        struct {
            memory::Pointer<Channel> channel;
        } get_channel;
        struct {
            memory::Pointer<MultiWriterChannel> channel;
        } get_multi_channel;
        struct {
            memory::Pointer<RawChannel> channel;
        } get_raw_channel;
    };

    static_assert(sizeof(memory::Pointer<int>) == sizeof(uint64_t));

    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;
    static ControlPlaneStatus control_plane_status = ControlPlaneStatus::kStopped;

    static int system_id;
    static int control_plane_sock;
    static int sock_to_control_plane;

    static void ControlPlane() {
        {
            std::unique_lock _(control_plane_state_lock);
            control_plane_started = true;
            cond_control_plane_started.notify_all();
        }
        memory::InitializeSubsystemThread();
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

                    try {
                        readAll(fd, reinterpret_cast<char*>(&request), sizeof(request));
                    } catch (std::runtime_error& err_info) {
                        peer_closed = true;
                    }

                    if (!peer_closed) {
                        switch (request.type) {
                            case RpcRequestType::kGetChannel: {
                                auto name = std::string(request.get_channel.name);
                                if (name.length() != 0)
                                    name = "__omni_channel_" + name;
                                auto channel = reinterpret_cast<Channel*>(memory::AllocateNamedSharedForThread(name, sizeof(Channel), request.get_channel.thread_id));
                                if (channel) {
                                    if (!channel->initialized_) {
                                        channel->initialized_ = true;

                                        channel->writer_token_ = memory::Pointer(token::CreateToken());
                                        channel->reader_token_ = memory::Pointer(token::CreateToken());

                                        std::string raw_channel_name = name + "_raw_";
                                        auto raw_channel = reinterpret_cast<RawChannel*>(memory::AllocateNamedSharedForThread(raw_channel_name, sizeof(RawChannel), request.get_channel.thread_id));

                                        channel->Init(raw_channel);
                                    }

                                    resp.status = RpcResponseStatus::kSuccess;
                                    resp.get_channel.channel = memory::Pointer(channel);
                                } else {
                                    resp.status = RpcResponseStatus::kFail;
                                }
                                break;
                            }
                            case RpcRequestType::kGetRawChannel: {
                                auto name = std::string(request.get_raw_channel.name);
                                auto thread_id = request.get_raw_channel.thread_id;
                                if (!name.starts_with("__omni_mul_channel_"))
                                    name = "__omni_raw_channel_" + name;
                                auto channel = reinterpret_cast<RawChannel*>(memory::AllocateNamedSharedForThread(name, sizeof(RawChannel), thread_id));
                                if (channel) {
                                    if (!channel->initialized_) {
                                        channel->initialized_ = true;
                                        channel->Init();
                                    }

                                    resp.status = RpcResponseStatus::kSuccess;
                                    resp.get_raw_channel.channel = memory::Pointer(channel);
                                } else {
                                    resp.status = RpcResponseStatus::kFail;
                                }
                                break;
                            }
                            case RpcRequestType::kGetMultiWriterChannel: {
                                auto name = std::string(request.get_channel.name);
                                if (name.length() != 0)
                                    name = "__omni_mul_channel_" + name;
                                auto channel = reinterpret_cast<MultiWriterChannel*>(memory::AllocateNamedShared(name, sizeof(MultiWriterChannel)));
                                if (channel) {
                                    if (!channel->initialized) {
                                        channel->initialized = true;

                                        channel->reader_token_ = memory::Pointer(token::CreateToken());
                                        strcpy(channel->name, name.c_str());
                                        channel->Init();
                                    }
                                    resp.status = RpcResponseStatus::kSuccess;
                                    resp.get_multi_channel.channel = memory::Pointer(channel);
                                } else {
                                    resp.status = RpcResponseStatus::kFail;
                                }
                                break;
                            }
                        }
                        resp.id = request.id;

                        try {
                            writeAll(fd, reinterpret_cast<char*>(&resp), sizeof(resp));
                        } catch (std::runtime_error& err_info) {
                            peer_closed = true;
                        }
                    }

                    if (peer_closed) {

                    }
                }
            }
        }
    }

    void StartControlPlane() {
        std::unique_lock _(control_plane_state_lock);
        if (control_plane_started)
            throw std::runtime_error("There is multiple control plane threads");
        control_plane_status = ControlPlaneStatus::kStarting;

        std::string control_plane_sock_name = "";
        system_id = memory::GetControlPlaneId();
        control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_channel_sock" +
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

    int Channel::Write(const void* data) {
        if (!writer_token_->CheckToken())
            writer_token_->AcquireToken();
        return raw_channel_->Write(data);
    }

    int RawChannel::Write(const void* data) {
        const int next_val = (writer_write_pos_ + 1) == kChannelSize ? 0 : writer_write_pos_ + 1;
        if (next_val == writer_read_pos_) {
            if (next_val == read_pos_) {
                return -1;
            }
            writer_read_pos_ = read_pos_;
        }
        auto region_meta = reinterpret_cast<memory::RegionMeta*>((uint8_t*)data - memory::kMetaHeadroomSize);
        region_meta->process_id = 0;
#if defined (OMNIMEM_BACKEND_DPDK)
        ring_ptr_[writer_write_pos_] = region_meta;
#else
        ring_offset_[writer_write_pos_] = (uint8_t*)region_meta - virt_base_addrs[memory::process_id];
#endif
        writer_write_pos_ = next_val;
        writer_batch_count_ ++;
        if (writer_batch_count_ >= kBatchSize) [[unlikely]] {
            writer_batch_count_ = 0;
            write_pos_ = next_val;
            return 1;
        }
        return 0;
    }

    void* Channel::Read() {
        if (!reader_token_->CheckToken())
            reader_token_->AcquireToken();
        return raw_channel_->Read();
    }

    void* RawChannel::Read() {
        if (reader_read_pos_ == reader_write_pos_) {
            if (reader_read_pos_ == write_pos_) {
                return nullptr;
            }
            reader_write_pos_ = write_pos_;
        }
#if defined (OMNIMEM_BACKEND_DPDK)
        auto ret = ring_ptr_[reader_read_pos_];
#else
        auto ret = reinterpret_cast<memory::RegionMeta*>(virt_base_addrs[memory::process_id] + ring_offset_[reader_read_pos_]);
#endif
        ret->process_id = memory::process_id;
        reader_read_pos_ = (reader_read_pos_ + 1) == kChannelSize ? 0 : reader_read_pos_ + 1;
        reader_batch_count_ ++;
        if (reader_batch_count_ >= kBatchSize) [[unlikely]] {
            reader_batch_count_ = 0;
            read_pos_ = reader_read_pos_;
        }
        return (uint8_t*)ret + memory::kMetaHeadroomSize;
    }

    bool RawChannel::IsReadable() {
        return false;
    }

    int Channel::Flush() {
        if (!writer_token_->CheckToken())
            writer_token_->AcquireToken();
        return raw_channel_->Flush();
    }

    int RawChannel::Flush() {
        if (writer_batch_count_ > 0) {
            write_pos_ = writer_write_pos_;
            writer_batch_count_ = 0;
            return 1;
        }
        return 0;
    }

    void Channel::Init(RawChannel* raw_channel) {
        raw_channel_ = memory::Pointer(raw_channel);
        return raw_channel->Init();
    }

    void RawChannel::Init() {
        reader_read_pos_ = 0;
        reader_write_pos_ = 0;
        reader_batch_count_ = 0;

        writer_read_pos_ = 0;
        writer_write_pos_ = 0;
        writer_batch_count_ = 0;

        read_pos_ = 0;
        write_pos_ = 0;
    }

    int MultiWriterChannel::Write(const void* data) {
        auto region_meta = reinterpret_cast<memory::RegionMeta*>((uint8_t*)data - memory::kMetaHeadroomSize);
        region_meta->process_id = 0;
        if (!channel_ptrs_[memory::thread_id].Get()) [[unlikely]] {
            channel_ptrs_[memory::thread_id] = \
                memory::Pointer(GetRawChannel(std::string(name) + "_raw_" + std::to_string(memory::thread_id)));
        }
        auto& channel = channel_ptrs_[memory::thread_id];
        const auto ret_val = channel->Write(data);
        if (ret_val == 1) [[unlikely]] {
            auto current_pos = memory::thread_id + (memory::kMaxThread - 1);
            while (current_pos) {
                write_tick_[current_pos] = memory::thread_id;
                current_pos >>= 1;
            }
        } else if (!ret_val) [[likely]] {
            return 0;
        }
        return -1;
    }

    void MultiWriterChannel::Flush() {
        if (channel_ptrs_[memory::thread_id].Get()) [[likely]] {
            auto channel = channel_ptrs_[memory::thread_id];
            if (channel->Flush() == 1) [[likely]] {
                auto current_pos = memory::thread_id + (memory::kMaxThread - 1);
                while (current_pos) {
                    write_tick_[current_pos] = memory::thread_id;
                    current_pos >>= 1;
                }
            }
        }
    }

    void* MultiWriterChannel::Read() {
        if (!reader_token_->CheckToken())
            reader_token_->AcquireToken();

        uint64_t last_pos;
        if (current_channel_thread_id_ != 0) [[likely]] {
            auto& channel = current_channel_ptr_;
            auto ret = channel->Read();
            if (ret) [[likely]]
                return ret;
            else {
                last_pos = current_channel_thread_id_;
                current_channel_ptr_ = nullptr;
                current_channel_thread_id_ = 0;
            }
        }

        last_pos += memory::kMaxThread - 1;
        while (last_pos != 1) {
            auto this_side_c = write_tick_[last_pos];
            auto other_side_c = write_tick_[last_pos^1];
            if (other_side_c && channel_ptrs_[other_side_c]->IsReadable()) [[likely]] {
                current_channel_ptr_ = channel_ptrs_[other_side_c];
                current_channel_thread_id_ = other_side_c;
                break;
            } else if (channel_ptrs_[this_side_c]->IsReadable()) {
                current_channel_ptr_ = channel_ptrs_[this_side_c];
                current_channel_thread_id_ = this_side_c;
                break;
            }
            last_pos >>= 1;
        }

        if (current_channel_thread_id_ != 0) [[likely]] {
            auto& channel = current_channel_ptr_;
            auto ret = channel->Read();
            if (ret) [[likely]]
                return ret;
            else {
                current_channel_ptr_ = nullptr;
                current_channel_thread_id_ = 0;
            }
        }
        return nullptr;
    }

    struct RpcRequestMeta {
        bool cond_rpc_finished;
        std::condition_variable cond_rpc_changed;
        std::mutex cond_rpc_lock;
        RpcResponse resp;
    };

    static int control_plane_id;
    static std::thread* rpc_response_receiver;
    static std::map<int, RpcRequestMeta*> id_to_rpc_meta;
    static thread_local RpcRequestMeta local_rpc_meta{};
    static thread_local RpcRequest local_rpc_req;
    static std::mutex rpc_request_lock;
    static int rpc_id;

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

    void InitializeSubsystem() {
        control_plane_id = memory::GetControlPlaneId();
        auto control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_channel_sock" +
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

    RpcResponse SendLocalRpcMessage() {
        {
            std::unique_lock<std::mutex> _(rpc_request_lock);
            local_rpc_req.id = ++rpc_id;
            while (id_to_rpc_meta.count(local_rpc_req.id))
                local_rpc_req.id = ++rpc_id;
            id_to_rpc_meta[local_rpc_req.id] = &local_rpc_meta;
            local_rpc_meta.cond_rpc_finished = false;
            writeAll(sock_to_control_plane, reinterpret_cast<char*>(&local_rpc_req), sizeof(RpcRequest));
        }
        
        {
            std::unique_lock<std::mutex> _(local_rpc_meta.cond_rpc_lock);
            local_rpc_meta.cond_rpc_changed.wait(_, [](){
                return local_rpc_meta.cond_rpc_finished;
            });
        }

        return local_rpc_meta.resp;
    }

    RawChannel* GetRawChannel(const std::string& name) {
        local_rpc_req.type = RpcRequestType::kGetRawChannel;
        strcpy(local_rpc_req.get_raw_channel.name, name.c_str());
        local_rpc_req.get_raw_channel.thread_id = memory::thread_id;
        auto resp = SendLocalRpcMessage();
        if (resp.status == RpcResponseStatus::kSuccess)
            return resp.get_raw_channel.channel.Get();
        return nullptr;
    }

    Channel* GetChannel(const std::string& name) {
        local_rpc_req.type = RpcRequestType::kGetChannel;
        strcpy(local_rpc_req.get_channel.name, name.c_str());
        local_rpc_req.get_channel.thread_id = memory::thread_id;
        auto resp = SendLocalRpcMessage();
        if (resp.status == RpcResponseStatus::kSuccess)
            return resp.get_channel.channel.Get();
        return nullptr;
    }

    MultiWriterChannel* GetMultiWriterChannel(const std::string& name) {
        local_rpc_req.type = RpcRequestType::kGetMultiWriterChannel;
        strcpy(local_rpc_req.get_channel.name, name.c_str());
        local_rpc_req.get_channel.thread_id = memory::thread_id;
        auto resp = SendLocalRpcMessage();
        if (resp.status == RpcResponseStatus::kSuccess)
            return resp.get_multi_channel.channel.Get();
        return nullptr;
    }

    void MultiWriterChannel::Init() {
        for (int i = 0; i < memory::kMaxThread; i ++)
            channel_ptrs_[i] = memory::Pointer<RawChannel>(nullptr);;
        current_channel_thread_id_ = 0;
        current_channel_ptr_ = memory::Pointer<RawChannel>(nullptr);
    }
}