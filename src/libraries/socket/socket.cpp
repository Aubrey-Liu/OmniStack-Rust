#include <omnistack/socket/socket.h>
#include <omnistack/node.h>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>

#include <mutex>

#if defined(OMNIMEM_BACKEND_DPDK)
#include <rte_memcpy.h>
#endif

namespace omnistack::socket {
    enum class FileDescriptorType {
        kClosed = 0,
        kUnknown,
        kLinux,
        kBasic,
        kEvent
    };

    struct FileDescriptor {
        FileDescriptorType type;
        int fd;
        union {
            node::BasicNode* basic_node;
            node::EventNode* event_ndoe;
            int system_fd;
        };
        struct {
            int domain;
            int type;
        } info;
        bool blocking;
        FileDescriptor* next;
        size_t max_packet_size;
        packet::Packet* packet_cache;

        void Init() {
            blocking = true;
            next = nullptr;
            type = FileDescriptorType::kUnknown;
            packet_cache = nullptr;
        }

        void InitPost() {
            max_packet_size = common::kMtu;
            switch (info.domain) {
                case AF_INET:
                    max_packet_size -= sizeof(common::Ipv4Header);
                    break;
                case AF_INET6:
                    max_packet_size -= sizeof(common::Ipv6Header);
                    break;
            }
            switch (info.type) {
                case SOCK_STREAM:
                    max_packet_size -= sizeof(common::TcpHeader) + 16;
                    break;
                case SOCK_DGRAM:
                    max_packet_size -= sizeof(common::UdpHeader);
                    break;
            }
        }
    };

    static std::mutex global_fd_lock;
    static FileDescriptor* global_fd_link_list = nullptr;
    static int global_fd_count = 0;
    static FileDescriptor* global_fd_list[65536];
    static thread_local FileDescriptor* local_fd_list = nullptr;
    static thread_local FileDescriptor* local_fd_free_list = nullptr;
    static thread_local int local_fd_free_count = 0;
    constexpr int num_fd_allocate = 64;

    static inline int GenerateFd() {
        if (local_fd_list == nullptr) {
            int begin_idx, end_idx;
            {
                std::unique_lock<std::mutex> lock(global_fd_lock);
                if (global_fd_link_list == nullptr) {
                    begin_idx = global_fd_count + 1;
                    end_idx = begin_idx + num_fd_allocate - 1;
                    global_fd_count = end_idx;
                } else {
                    for (int i = 0; i < num_fd_allocate; ++i) {
                        auto cur = global_fd_link_list;
                        global_fd_link_list = global_fd_link_list->next;
                        cur->next = local_fd_list;
                        local_fd_list = cur;
                    }
                }
            }
            if (!local_fd_list) [[unlikely]] {
                local_fd_list = new FileDescriptor[num_fd_allocate];
                for (int i = 0; i < num_fd_allocate - 1; ++i)
                    local_fd_list[i].next = &local_fd_list[i + 1];
                for (int i = 0; i < num_fd_allocate; ++i) {
                    local_fd_list[i].type = FileDescriptorType::kClosed;
                    local_fd_list[i].fd = begin_idx + i;
                    global_fd_list[begin_idx + i] = &local_fd_list[i];
                }
            }
        }
        auto cur = local_fd_list;
        auto ret = local_fd_list->fd;
        local_fd_list = cur->next;
        cur->Init();
        return ret;
    }

    static inline void ReleaseFd(int fd) {
        if (local_fd_free_count == num_fd_allocate) {
            auto cur = local_fd_free_list;
            for (int i = 1; i < num_fd_allocate; ++i)
                cur = cur->next;
            {
                std::unique_lock<std::mutex> lock(global_fd_lock);
                cur->next = global_fd_link_list;
                global_fd_link_list = local_fd_free_list;
            }
            local_fd_free_list = nullptr;
            local_fd_free_count = 0;
        }
        auto cur = global_fd_list[fd];
        cur->next = local_fd_free_list;
        cur->type = FileDescriptorType::kClosed;
        local_fd_free_list = cur;
    }

    namespace bsd {
        int socket(int domain, int type, int protocol) {
            static thread_local unsigned int local_seed = rand();

            int ret = GenerateFd();
            auto cur_fd = global_fd_list[ret];
            
            cur_fd->info.domain = domain;
            cur_fd->info.type = type;
            if (domain == AF_INET && (type == SOCK_STREAM || type == SOCK_DGRAM)) [[likely]] {
                cur_fd->type = FileDescriptorType::kBasic;
                cur_fd->basic_node = node::CreateBasicNode(rand_r(&local_seed) % node::GetNumNodeUser());
            } else {
                cur_fd->type = FileDescriptorType::kLinux;
                cur_fd->system_fd = ::socket(domain, type, protocol);
            }
            return ret;
        }

        int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
            auto cur_fd = global_fd_list[sockfd];
            if (addr->sa_family != cur_fd->info.domain) [[unlikely]] return -1;
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto addr_ = reinterpret_cast<const struct sockaddr_in*>(addr);
                    auto basic_node = cur_fd->basic_node;
                    auto new_node_info = node::NodeInfo();
                    new_node_info.network_layer_type = node::NetworkLayerType::kIPv4;
                    new_node_info.transport_layer_type = 
                        cur_fd->info.type == SOCK_STREAM ? node::TransportLayerType::kTCP : node::TransportLayerType::kUDP;
                    new_node_info.network.Set(addr_->sin_addr.s_addr, 0);
                    new_node_info.transport.sport = addr_->sin_port;
                    new_node_info.transport.dport = 0;
                    cur_fd->basic_node->UpdateInfo(new_node_info);
                    if (cur_fd->info.type == SOCK_DGRAM)
                        basic_node->PutIntoHashtable();
                    return 0;
                }
                [[unlikely]] case FileDescriptorType::kLinux: {
                    return ::bind(cur_fd->system_fd, addr, addrlen);
                }
            }
            return -1;
        }

        int listen(int sockfd, int backlog) { // Passive Node
            auto cur_fd = global_fd_list[sockfd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto basic_node = cur_fd->basic_node;
                    basic_node->ReadMulti(); // Manually Create MultiWriter Channel
                    basic_node->PutIntoHashtable();
                    return 0;
                }
                case FileDescriptorType::kLinux: {
                    return ::listen(cur_fd->system_fd, backlog);
                }
            }
            return -1;
        }

        int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
            auto cur_fd = global_fd_list[sockfd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto basic_node = cur_fd->basic_node;
                    auto new_node = (node::BasicNode*)basic_node->ReadMulti();
                    while (!new_node && cur_fd->blocking) [[unlikely]]
                        new_node = (node::BasicNode*)basic_node->ReadMulti();
                    if (new_node) [[likely]] {
                        int new_fd = GenerateFd();
                        auto new_fd_ptr = global_fd_list[new_fd];
                        new_fd_ptr->basic_node = new_node;
                        new_fd_ptr->type = FileDescriptorType::kBasic;
                        new_fd_ptr->info = cur_fd->info;
                        new_fd_ptr->InitPost();
                        if (addr != nullptr) {
                            if (new_fd_ptr->info.domain == AF_INET) [[likely]] {
                                auto addr_ = reinterpret_cast<struct sockaddr_in*>(addr);
                                addr_->sin_family = AF_INET;
                                addr_->sin_addr.s_addr = new_node->info_.network.ipv4.dip;
                                addr_->sin_port = new_node->info_.transport.dport;
                                *addrlen = sizeof(struct sockaddr_in);
                            } else {
                                *addrlen = 0;
                            }
                        }
                        return new_fd;
                    } else if (!cur_fd->blocking) [[unlikely]] {
                        errno = EWOULDBLOCK;
                        return -1;
                    }
                    errno = EINVAL;
                    return -1;
                }
                case FileDescriptorType::kLinux: {
                    return ::accept(cur_fd->system_fd, addr, addrlen);
                }
            }
            return -1;
        }

        ssize_t read(int fd, void *buf, size_t count) {
            auto cur_fd = global_fd_list[fd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto packet = cur_fd->packet_cache == nullptr ? 
                        cur_fd->basic_node->Read() :  cur_fd->packet_cache;
                    cur_fd->packet_cache = nullptr;
                    while (packet != nullptr && cur_fd->blocking) [[unlikely]] {
                        packet = cur_fd->basic_node->Read();
                    }

                    if (packet != nullptr) [[likely]] {
                        auto cur_length = packet->length_ - packet->offset_;
                        auto ret = std::min(count, static_cast<size_t>(cur_length));
    #if defined(OMNIMEM_BACKEND_DPDK)
                        rte_memcpy(buf,
                            packet->data_ + packet->offset_, ret);
    #else
                        memcpy(buf,
                            packet->data_ + packet->offset_, ret);
    #endif
                        if (cur_length > ret && cur_fd->info.type == SOCK_STREAM) [[unlikely]] {
                            packet->offset_ += ret;
                            cur_fd->packet_cache = packet;
                        } else [[likely]] {
                            packet->Release();
                        }
                        return ret;
                    } else if (!cur_fd->blocking) [[unlikely]] {
                        errno = EWOULDBLOCK;
                        return -1;
                    }
                    errno = EINVAL;
                    return 0;
                }
                case FileDescriptorType::kLinux:
                    return ::read(cur_fd->system_fd, buf, count);
            }
            return 0;
        }
        // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
        // ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags,
        //                 struct sockaddr* src_addr, socklen_t* addrlen);

        ssize_t write(int fd, const void *buf, size_t count) {
            auto cur_fd = global_fd_list[fd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    if (cur_fd->basic_node->peer_closed_) [[unlikely]] {
                        errno = EPIPE;
                        return -1;
                    }

                    auto ret = static_cast<ssize_t>(0);
                    while (ret < count) {
                        auto packet = node::BasicNode::packet_pool_->Allocate();
                        auto cur_size = std::min(count, cur_fd->max_packet_size);
    #if defined(OMNIMEM_BACKEND_DPDK)
                        rte_memcpy(packet->data_ + packet->offset_, 
                            reinterpret_cast<const char*>(buf) + ret, cur_size);
    #else
                        memcpy(packet->data_ + packet->offset_, 
                            reinterpret_cast<const char*>(buf) + ret, cur_size);
    #endif
                        packet->length_ += cur_size;
                        packet->node_ = cur_fd->basic_node;
                        cur_fd->basic_node->WriteBottom(packet);
                        ret += cur_size;
                    }
                    node::FlushBottom();
                    return ret;
                }
                case FileDescriptorType::kLinux:
                    return ::write(cur_fd->system_fd, buf, count);
            }
            return 0;
        }
        // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        // ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
        //             const struct sockaddr* dest_addr, socklen_t addrlen);
    
        void close(int fd) {
            auto cur_fd = global_fd_list[fd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    cur_fd->basic_node->CloseRef();
                    ReleaseFd(fd);
                    break;
                }
                case FileDescriptorType::kLinux:
                    ::close(cur_fd->system_fd);
                    ReleaseFd(fd);
                    break;
            }
        }
    }

    namespace fast {
        int listen(int sockfd, int backlog, const std::vector<uint8_t>& graph_ids) { // Passive Node
            auto cur_fd = global_fd_list[sockfd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto basic_node = cur_fd->basic_node;

                    basic_node->num_graph_usable_ = graph_ids.size();
                    for (int i = 0; i < graph_ids.size(); ++i)
                        basic_node->graph_usable_[i] = graph_ids[i];

                    basic_node->ReadMulti(); // Manually Create MultiWriter Channel
                    basic_node->PutIntoHashtable();
                    return 0;
                }
                case FileDescriptorType::kLinux: {
                    return ::listen(cur_fd->system_fd, backlog);
                }
            }
            return -1;
        }

        ssize_t read(int fd, packet::Packet** packet_) {
            auto cur_fd = global_fd_list[fd];
            switch (cur_fd->type) {
                [[likely]] case FileDescriptorType::kBasic: {
                    auto packet = cur_fd->packet_cache == nullptr ? 
                        cur_fd->basic_node->Read() :  cur_fd->packet_cache;
                    cur_fd->packet_cache = nullptr;
                    while (packet == nullptr && cur_fd->blocking) [[unlikely]] {
                        packet = cur_fd->basic_node->Read();
                    }

                    if (packet != nullptr) [[likely]] {
                        *packet_ = packet;
                        return packet->length_ - packet->offset_;
                    } else if (!cur_fd->blocking) [[unlikely]] {
                        errno = EWOULDBLOCK;
                        return -1;
                    }
                    return 0;
                }
                case FileDescriptorType::kLinux:
                    errno = EINVAL;
                    return -1;
            }
            return 0;
        }
        packet::Packet* recv(int sockfd, int flags);
        ssize_t recvfrom(int sockfd, packet::Packet** packet, int flags,
            struct sockaddr* src_addr, socklen_t* addrlen) {
            auto flg = read(sockfd, packet);
            if (flg > 0 && src_addr != nullptr) {
                auto udp_header = reinterpret_cast<common::UdpHeader*>((*packet)->GetHeaderPayload(2));
                auto ipv4_header = reinterpret_cast<common::Ipv4Header*>((*packet)->GetHeaderPayload(1));
                auto src_addr_ = reinterpret_cast<struct sockaddr_in*>(src_addr);
                src_addr_->sin_family = AF_INET;
                src_addr_->sin_addr.s_addr = ipv4_header->src;
                src_addr_->sin_port = udp_header->sport;
                *addrlen = sizeof(struct sockaddr_in);
            }
            return flg;
        }

        packet::Packet* write_begin(int fd) {
            auto cur_fd = global_fd_list[fd];
            if (cur_fd->type == FileDescriptorType::kBasic) [[likely]] {
                return node::BasicNode::packet_pool_->Allocate();
            }
            errno = EINVAL;
            return nullptr;
        }

        ssize_t write(int fd, packet::Packet* buf) {
            auto cur_fd = global_fd_list[fd];
            if (cur_fd->type == FileDescriptorType::kBasic) [[likely]] {
                if (cur_fd->basic_node->peer_closed_) [[unlikely]] {
                    errno = EPIPE;
                    return 0;
                }
                buf->node_ = cur_fd->basic_node;
                cur_fd->basic_node->WriteBottom(buf);
                auto ret = buf->length_ - buf->offset_;
                node::FlushBottom();
                return ret;
            }
            errno = EINVAL;
            return -1;
        }
        // ssize_t send(int sockfd, packet::Packet* buf, int flags);
        ssize_t sendto(int sockfd, packet::Packet* buf, int flags,
            const struct sockaddr* dest_addr, socklen_t addrlen) {
            auto cur_fd = global_fd_list[sockfd];
            auto cur_node = cur_fd->basic_node;

            buf->peer_addr_ = *(struct sockaddr_in*)dest_addr;
            cur_node->WriteBottom(buf);
            auto ret = buf->length_ - buf->offset_;
            node::FlushBottom();
            return ret;
        }
    }    
}