#ifndef OMNISTACK_SOCKET_C_H
#define OMNISTACK_SOCKET_C_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif
    int omni_epoll_create(int size);
    int omni_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
    int omni_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

    void omni_close(int fd);

    int omni_socket(int domain, int type, int protocol);
    int omni_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int omni_listen(int sockfd, int backlog);
    int omni_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

    int omni_fcntl(int fd, int cmd, ... /* arg */ );

    void omni_new_thread();
    void omni_bind_cpu(int cpu);

    ssize_t omni_read(int fd, void *buf, size_t count);
    ssize_t omni_write(int fd, const void *buf, size_t count);
#ifdef __cplusplus
}
#endif

#endif