//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
#define OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP

#include <omnistack/memory/memory.h>

namespace omnistack::data_plane::tcp_common {

    class TcpReceiveBuffer {
    public:
        TcpReceiveBuffer() = delete;
        TcpReceiveBuffer(const TcpReceiveBuffer&) = delete;
        TcpReceiveBuffer(TcpReceiveBuffer&&) = delete;

        static TcpReceiveBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpReceiveBuffer* buffer);
    };

    class TcpSendBuffer {
    public:
        TcpSendBuffer() = delete;
        TcpSendBuffer(const TcpSendBuffer&) = delete;
        TcpSendBuffer(TcpSendBuffer&&) = delete;

        static TcpSendBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpSendBuffer* buffer);
    };

}

#endif //OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
