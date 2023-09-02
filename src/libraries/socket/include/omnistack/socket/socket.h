#ifndef OMNISTACK_SOCKET_H
#define OMNISTACK_SOCKET_H

#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <omnistack/packet/packet.hpp>

namespace omnistack {
    namespace socket {
        namespace bsd {
            int socket(int domain, int type, int protocol);

            int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
            int listen(int sockfd, int backlog);
            int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

            ssize_t read(int fd, void *buf, size_t count);
            ssize_t recv(int sockfd, void *buf, size_t len, int flags);
            ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags,
                            struct sockaddr* src_addr, socklen_t* addrlen);

            ssize_t write(int fd, const void *buf, size_t count);
            ssize_t send(int sockfd, const void *buf, size_t len, int flags);
            ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
                        const struct sockaddr* dest_addr, socklen_t addrlen);
            
            int epoll_create(int size);
            int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
            int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

            int fcntl(int fd, int cmd, ... /* arg */ );
            
            void close(int fd);
        }
        namespace fast {
            int listen(int sockfd, int backlog, const std::vector<uint8_t>& graph_ids);

            ssize_t read(int fd, packet::Packet** packet);
            ssize_t recv(int sockfd, packet::Packet** packet, int flags);
            ssize_t recvfrom(int sockfd, packet::Packet** packet, int flags,
                            struct sockaddr* src_addr, socklen_t* addrlen);

            packet::Packet* write_begin(int fd);
            ssize_t write(int fd, packet::Packet* buf);
            ssize_t send(int sockfd, packet::Packet* buf, int flags);
            ssize_t sendto(int sockfd, packet::Packet* buf, int flags,
                        const struct sockaddr* dest_addr, socklen_t addrlen);
        }
    }
}

#endif