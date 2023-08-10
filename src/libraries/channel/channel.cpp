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

namespace omnistack::channel {
    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;
    static ControlPlaneStatus control_plane_status = ControlPlaneStatus::kStopped;

    static int system_id;
    static int control_plane_sock;
    static int sock_to_control_plane;

    static uint8_t** virt_base_addrs;

    static void ControlPlane() {
        {
            std::unique_lock _(control_plane_state_lock);
            control_plane_started = true;
            cond_control_plane_started.notify_all();
        }
        memory::InitializeSubsystemThread();
#if !defined(OMNIMEM_BACKEND_DPDK)
        virt_base_addrs = memory::GetVirtBaseAddrs();
#endif
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
                    int new_fd;
                    do {
                        new_fd = accept(control_plane_sock, nullptr, nullptr);
                        if (new_fd > 0) {
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
#if defined(OMNIMEM_BACKEND_DPDK)
        if (!writer_token_ptr_->CheckToken())
            writer_token_ptr_->AcquireToken();
#else
        auto writer_token = reinterpret_cast<token::Token*>(virt_base_addrs[memory::process_id] + writer_token_offset_);
        if (!writer_token->CheckToken())
            writer_token->AcquireToken();
#endif

        const int next_val = (writer_write_pos_ + 1) == kChannelSize ? 0 : writer_write_pos_ + 1;
        if (next_val == writer_read_pos_) {
            if (next_val == read_pos_) {
                return -1;
            }
            writer_read_pos_ = read_pos_;
        }
        auto region_meta = reinterpret_cast<memory::RegionMeta*>((uint8_t*)data - memory::kMetaHeadroomSize);
        region_meta->process_id = 0;
#if defined(OMNIMEM_BACKEND_DPDK)
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
#if defined(OMNIMEM_BACKEND_DPDK)
        if (!reader_token_ptr_->CheckToken())
            reader_token_ptr_->AcquireToken();
#else
        auto reader_token = reinterpret_cast<token::Token*>(virt_base_addrs[memory::process_id] + reader_token_offset_);
        if (!reader_token->CheckToken())
            reader_token->AcquireToken();
#endif

        if (reader_read_pos_ == reader_write_pos_) {
            if (reader_read_pos_ == write_pos_) {
                return nullptr;
            }
            reader_write_pos_ = write_pos_;
        }
#if defined(OMNIMEM_BACKEND_DPDK)
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

    int Channel::Flush() {
#if defined(OMNIMEM_BACKEND_DPDK)
        if (!writer_token_ptr_->CheckToken())
            writer_token_ptr_->AcquireToken();
#else
        auto writer_token = reinterpret_cast<token::Token*>(virt_base_addrs[memory::process_id] + writer_token_offset_);
        if (!writer_token->CheckToken())
            writer_token->AcquireToken();
#endif

        if (writer_batch_count_ > 0) {
            write_pos_ = writer_write_pos_;
            writer_batch_count_ = 0;
            return 1;
        }
        return 0;
    }
}