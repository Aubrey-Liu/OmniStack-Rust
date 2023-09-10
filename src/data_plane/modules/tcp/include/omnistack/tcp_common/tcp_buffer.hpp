//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
#define OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP

#include <omnistack/memory/memory.h>
#include <omnistack/packet/packet.hpp>
#include <omnistack/common/logger.h>
#include <queue>

namespace omnistack::data_plane::tcp_common {

    using namespace packet;

    inline bool TcpGreaterUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    inline bool TcpLessUint32(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) < 0;
    }

    class TcpReceiveBuffer {
    public:
        TcpReceiveBuffer(const TcpReceiveBuffer&) = delete;
        TcpReceiveBuffer(TcpReceiveBuffer&&) = delete;

        static TcpReceiveBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpReceiveBuffer* buffer);
    
        Packet* Pop(uint32_t& seq, Packet* trigger_packet);

        void Push(uint32_t seq, Packet* packet);

        uint32_t size_() const { return buffer_.size(); }

    private:
        TcpReceiveBuffer() = default;
        ~TcpReceiveBuffer() = default;

        struct BufferEntry {
            uint32_t seq;
            Packet* packet;
        };

        friend bool operator<(const BufferEntry& a, const BufferEntry& b) {
            return TcpGreaterUint32(a.seq, b.seq);
        }

        std::priority_queue<BufferEntry> buffer_;
    };

    class TcpSendBuffer {
    public:
        TcpSendBuffer(const TcpSendBuffer&) = delete;
        TcpSendBuffer(TcpSendBuffer&&) = delete;

        static TcpSendBuffer* Create(memory::MemoryPool* buffer_pool);

        static void Destroy(memory::MemoryPool* buffer_pool, TcpSendBuffer* buffer);

        void PopSent(uint32_t bytes);

        void PushSent(Packet* packet);

        Packet* FrontSent() const;

        bool EmptySent() const;

        void PopUnsent();

        void PushUnsent(Packet* packet);

        Packet* FrontUnsent() const;

        bool EmptyUnsent() const;

    private:
        TcpSendBuffer() {
            sent_head = sent_tail = nullptr;
            unsent_head = unsent_tail = nullptr;
        }
        ~TcpSendBuffer() = default;

        Packet* sent_head;
        Packet* sent_tail;
        Packet* unsent_head;
        Packet* unsent_tail;
    };

    inline TcpReceiveBuffer* TcpReceiveBuffer::Create(memory::MemoryPool* buffer_pool) {
        auto addr = buffer_pool->Get();
        return new(addr) TcpReceiveBuffer();
    }

    inline void TcpReceiveBuffer::Destroy(memory::MemoryPool* buffer_pool, TcpReceiveBuffer* buffer) {
        buffer->~TcpReceiveBuffer();
        buffer_pool->Put(buffer);
    }

    inline Packet* TcpReceiveBuffer::Pop(uint32_t& seq, Packet* trigger_packet) {
        auto ret = trigger_packet;
        while(!buffer_.empty()) [[unlikely]] {
            auto entry = buffer_.top();
            auto begin_seq = entry.seq;
            if(TcpGreaterUint32(begin_seq, seq)) break;
            buffer_.pop();
            auto packet = entry.packet;
            auto end_seq = begin_seq + packet->GetLength();
            if(TcpGreaterUint32(end_seq, seq)) [[likely]] {
                packet->offset_ += seq - begin_seq;
                seq = end_seq;
                packet->next_packet_ = ret;
                ret = packet;
            }
            else packet->Release();
        }
        return ret;
    }

    inline void TcpReceiveBuffer::Push(uint32_t seq, Packet* packet) {
        buffer_.push({seq, packet});
    }

    inline TcpSendBuffer* TcpSendBuffer::Create(memory::MemoryPool* buffer_pool) {
        auto addr = buffer_pool->Get();
        return new(addr) TcpSendBuffer();
    }

    inline void TcpSendBuffer::Destroy(memory::MemoryPool* buffer_pool, TcpSendBuffer* buffer) {
        buffer->~TcpSendBuffer();
        buffer_pool->Put(buffer);
    }

    inline void TcpSendBuffer::PopSent(uint32_t bytes) {
        while(bytes > 0 && sent_head != nullptr) {
            if(sent_head == sent_tail && sent_head->next_packet_.Get() != nullptr)
                    OMNI_LOG(kDebug) << "error next_packet_\n";
            auto packet = sent_head;
            auto packet_bytes = packet->GetLength();
            if(packet_bytes <= bytes) {
                bytes -= packet_bytes;
                sent_head = packet->next_packet_.Get();
                packet->Release();
            }
            else {
                packet->offset_ += bytes;
                break;
            }
        }
        if(sent_head == nullptr) sent_tail = nullptr;
    }

    inline void TcpSendBuffer::PushSent(Packet* packet) {
        if(sent_head == nullptr && sent_tail != nullptr) OMNI_LOG(kDebug) << "error list\n";
        if(sent_head == nullptr) sent_head = sent_tail = packet;
        else {
            sent_tail->next_packet_ = packet;
            sent_tail = packet;
        }
    }

    inline Packet* TcpSendBuffer::FrontSent() const {
        return sent_head;
    }

    inline bool TcpSendBuffer::EmptySent() const {
        return sent_head == nullptr;
    }

    inline void TcpSendBuffer::PopUnsent() {
        auto head = unsent_head->next_packet_.Get();
        if(unsent_head == unsent_tail && head != nullptr) OMNI_LOG(kDebug) << "error unsent list\n";
        unsent_head->next_packet_ = nullptr;
        unsent_head = head;
        if(unsent_head == nullptr) unsent_tail ==nullptr;
    }

    inline void TcpSendBuffer::PushUnsent(Packet* packet) {
        if(unsent_head == nullptr) unsent_head = unsent_tail = packet;
        else {
            unsent_tail->next_packet_ = packet;
            unsent_tail = packet;
        }
    }

    inline Packet* TcpSendBuffer::FrontUnsent() const {
        return unsent_head;
    }

    inline bool TcpSendBuffer::EmptyUnsent() const {
        return unsent_head == nullptr;
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_BUFFER_HPP
