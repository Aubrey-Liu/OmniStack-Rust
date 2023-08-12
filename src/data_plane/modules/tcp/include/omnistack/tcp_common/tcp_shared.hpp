//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
#define OMNISTACK_TCP_COMMON_TCP_SHARED_HPP

#include <omnistack/tcp_common/tcp_state.hpp>
#include <omnistack/tcp_common/tcp_constant.hpp>
#include <omnistack/hashtable/hashtable.hpp>

namespace omnistack::data_plane::tcp_common {

    class TcpSharedHandle {
    public:
        TcpSharedHandle() = delete;
        TcpSharedHandle(const TcpSharedHandle&) = delete;
        TcpSharedHandle(TcpSharedHandle&&) = delete;

        static TcpSharedHandle* Create(std::string_view name_prefix);

        static void Destroy(TcpSharedHandle* handle);

        TcpFlow* CreateFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);

        TcpFlow* GetFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);

        void AcquireFlow(TcpFlow* flow);

        void ReleaseFlow(TcpFlow* flow);

    private:
        hashtable::Hashtable* flow_table_;
        hashtable::Hashtable* listen_table_;
        memory::MemoryPool* flow_pool_;
        memory::MemoryPool* receive_buffer_pool_;
        memory::MemoryPool* send_buffer_pool_;
        uint32_t initialized_;
    };

    inline TcpSharedHandle* TcpSharedHandle::Create(std::string_view name_prefix) {
        auto name = std::string(name_prefix) + "_TcpSharedHandle";
        auto handle = static_cast<TcpSharedHandle*>(memory::AllocateNamedShared(name, sizeof(TcpSharedHandle)));
        if(handle->initialized_ == 0) {
            name = std::string(name_prefix) + "_FlowPool";
            handle->flow_pool_ = memory::AllocateMemoryPool(name, sizeof(TcpFlow), kTcpMaxFlowCount);
            name = std::string(name_prefix) + "_ReceiveBufferPool";
            handle->receive_buffer_pool_ = memory::AllocateMemoryPool(name, sizeof(TcpReceiveBuffer), kTcpMaxFlowCount);
            name = std::string(name_prefix) + "_SendBufferPool";
            handle->send_buffer_pool_ = memory::AllocateMemoryPool(name, sizeof(TcpSendBuffer), kTcpMaxFlowCount);
            handle->flow_table_ = hashtable::Hashtable::Create(kTcpFlowTableSize, 32);
        }
        handle->initialized_ ++;
        return handle;
    }
    
    static inline void HashtableFreeCallback(const void* key, void* value, void* param) {
        auto flow = static_cast<TcpFlow*>(value);
        auto handle = static_cast<TcpSharedHandle*>(param);
        handle->ReleaseFlow(flow);
    }

    inline void TcpSharedHandle::Destroy(TcpSharedHandle* handle) {
        handle->initialized_ --;
        if(handle->initialized_ == 0) {
            handle->flow_table_->Foreach(HashtableFreeCallback, handle);
            hashtable::Hashtable::Destroy(handle->flow_table_);
            memory::FreeMemoryPool(handle->flow_pool_);
            memory::FreeMemoryPool(handle->receive_buffer_pool_);
            memory::FreeMemoryPool(handle->send_buffer_pool_);
        }
        memory::FreeNamedShared(handle);
    }

    inline TcpFlow* TcpSharedHandle::CreateFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
        auto flow = TcpFlow::Create(flow_pool_, receive_buffer_pool_, send_buffer_pool_, local_ip, remote_ip, local_port, remote_port);
        flow_table_->InsertKey(flow);
        return flow;
    }

    inline TcpFlow* TcpSharedHandle::GetFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
        static thread_local TcpFlow* key;
        key->local_ip_ = local_ip;
        key->remote_ip_ = remote_ip;
        key->local_port_ = local_port;
        key->remote_port_ = remote_port;
        auto flow = static_cast<TcpFlow*>(flow_table_->Lookup(key));
        return flow;
    }

    inline void TcpSharedHandle::AcquireFlow(TcpFlow* flow) {
        flow->Acquire();
    }

    inline void TcpSharedHandle::ReleaseFlow(TcpFlow* flow) {
        if(flow->Release()) {
            flow_table_->Delete(flow);
            TcpFlow::Destroy(flow_pool_, receive_buffer_pool_, send_buffer_pool_, flow);
        }
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
