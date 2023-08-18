//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
#define OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP

#include <omnistack/memory/memory.h>
#include <omnistack/packet/packet.hpp>
#include <queue>

namespace omnistack::data_plane::tcp_common {

    using namespace packet;

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    class TcpReceiveBuffer {
    public:
        TcpReceiveBuffer(const TcpReceiveBuffer&) = delete;
        TcpReceiveBuffer(TcpReceiveBuffer&&) = delete;

        static TcpReceiveBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpReceiveBuffer* buffer);
    
        Packet* Pop(uint32_t& seq);

        void Push(uint32_t seq, Packet* packet);

    private:
        TcpReceiveBuffer() = default;
        ~TcpReceiveBuffer() = default;

        typedef std::pair<uint32_t, Packet*> BufferEntry;

        friend bool operator<(const BufferEntry& a, const BufferEntry& b) {
            return TcpGreaterUint32(a.first, b.first);
        }

        std::priority_queue<BufferEntry> buffer_;
    };

    class TcpSendBuffer {
    public:
        TcpSendBuffer(const TcpSendBuffer&) = delete;
        TcpSendBuffer(TcpSendBuffer&&) = delete;

        static TcpSendBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpSendBuffer* buffer);

    private:
        TcpSendBuffer() = default;
        ~TcpSendBuffer() = default;
    };

    inline TcpReceiveBuffer* TcpReceiveBuffer::Create(memory::MemoryPool* buffer_pool) {
        auto addr = buffer_pool->Get();
        return new(addr) TcpReceiveBuffer();
    }

    inline void TcpReceiveBuffer::Destroy(memory::MemoryPool* buffer_pool, TcpReceiveBuffer* buffer) {
        buffer->~TcpReceiveBuffer();
        buffer_pool->Put(buffer);
    }

    inline Packet* TcpReceiveBuffer::Pop(uint32_t& seq) {
        Packet* ret_head = nullptr;
        Packet* ret_tail = nullptr;
        while(!buffer_.empty()) [[unlikely]] {
            auto entry = buffer_.top();
            auto begin_seq = entry.first;
            if(TcpGreaterUint32(begin_seq, seq)) break;
            buffer_.pop();
            auto packet = entry.second;
            auto end_seq = begin_seq + packet->length_ - packet->offset_;
            if(TcpGreaterUint32(end_seq, seq)) [[likely]] {
                packet->offset_ += seq - begin_seq;
                seq = end_seq;
                if(ret_head == nullptr) ret_head = ret_tail = packet;
                else {
                    ret_tail->next_packet_ = packet;
                    ret_tail = packet;
                }
            }
            else packet->Release();
        }
        return ret_head;
    }

    inline void TcpReceiveBuffer::Push(uint32_t seq, Packet* packet) {
        buffer_.push(std::make_pair(seq, packet));
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
