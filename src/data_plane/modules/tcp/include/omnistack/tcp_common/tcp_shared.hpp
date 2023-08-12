//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
#define OMNISTACK_TCP_COMMON_TCP_SHARED_HPP

#include <omnistack/tcp_common/tcp_state.hpp>
#include <omnistack/tcp_common/tcp_constant.hpp>
#include <omnistack/hashtable/hashtable.hpp>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

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

        bool IsListen(uint32_t local_ip, uint16_t local_port);

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
            handle->flow_table_ = hashtable::Hashtable::Create(kTcpFlowTableSize, 12);
            handle->listen_table_ = hashtable::Hashtable::Create(kTcpFlowTableSize, 12);
        }
        handle->initialized_ ++;
        return handle;
    }
    
    inline void HashtableFreeCallback(const void* key, void* value, void* param) {
        auto flow = static_cast<TcpFlow*>(value);
        auto handle = static_cast<TcpSharedHandle*>(param);
        handle->ReleaseFlow(flow);
    }

    inline void TcpSharedHandle::Destroy(TcpSharedHandle* handle) {
        handle->initialized_ --;
        if(handle->initialized_ == 0) {
            handle->flow_table_->Foreach(HashtableFreeCallback, handle);
            hashtable::Hashtable::Destroy(handle->flow_table_);
            hashtable::Hashtable::Destroy(handle->listen_table_);
            memory::FreeMemoryPool(handle->flow_pool_);
            memory::FreeMemoryPool(handle->receive_buffer_pool_);
            memory::FreeMemoryPool(handle->send_buffer_pool_);
        }
        memory::FreeNamedShared(handle);
    }

    inline TcpFlow* TcpSharedHandle::CreateFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
        auto flow = TcpFlow::Create(flow_pool_, receive_buffer_pool_, send_buffer_pool_, local_ip, remote_ip, local_port, remote_port);
        flow_table_->Insert(flow, flow);
        return flow;
    }

    inline TcpFlow* TcpSharedHandle::GetFlow(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
        static thread_local TcpFlow* key;
        key->local_ip_ = local_ip;
        key->remote_ip_ = remote_ip;
        key->local_port_ = local_port;
        key->remote_port_ = remote_port;
        return static_cast<TcpFlow*>(flow_table_->Lookup(key));
    }

    inline bool TcpSharedHandle::IsListen(uint32_t local_ip, uint16_t local_port) {
        static thread_local TcpFlow* key;
        key->local_ip_ = local_ip;
        key->remote_ip_ = 0;
        key->local_port_ = local_port;
        key->remote_port_ = 0;
        return listen_table_->LookupKey(key);
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

    inline void DecodeOptions(PacketHeader* tcp, uint16_t *mss, uint8_t* wscale, uint8_t* sack_permit, uint32_t* sack_block, uint32_t* timestamp, uint32_t* timestamp_echo) {
        if(tcp->length_ > sizeof(TcpHeader)) [[likely]] {
            uint8_t* options = reinterpret_cast<uint8_t*>(tcp->data_) + sizeof(TcpHeader);
            uint8_t* options_end = reinterpret_cast<uint8_t*>(tcp->data_) + tcp->length_;
            while(options < options_end) {
                uint8_t kind = *options;
                switch (kind) {
                case TCP_OPTION_KIND_EOL:
                    options = options_end;
                    break;
                case TCP_OPTION_KIND_NOP:
                    options = options + 1;
                    break;
                case TCP_OPTION_KIND_MSS:
                    if(mss != nullptr) *mss = *reinterpret_cast<uint16_t*>(options + 2);
                    options = options + 4;
                    break;
                case TCP_OPTION_KIND_WSOPT:
                    if(wscale != nullptr) *wscale = *(options + 2);
                    options = options + 3;
                    break;
                case TCP_OPTION_KIND_SACK_PREMITTED:
                    if(sack_permit != nullptr) *sack_permit = 1;
                    options = options + 2;
                    break;
                case TCP_OPTION_KIND_SACK:
                    if(sack_block != nullptr) {
                        uint8_t* end = options + *(options + 1);
                        options = options + 2;
                        while(options < end) {
                            *sack_block = *reinterpret_cast<uint32_t*>(options);
                            sack_block ++;
                            options = options + 4;
                        }
                    } 
                    else options = options + *(options + 1);
                    break;
                case TCP_OPTION_KIND_TSPOT:
                    if(timestamp != nullptr) *timestamp = *reinterpret_cast<uint32_t*>(options + 2);
                    if(timestamp_echo != nullptr) *timestamp_echo = *reinterpret_cast<uint32_t*>(options + 6);
                    options = options + 10;
                    break;
                default:
                    options = options + *(options + 1);
                    break;
                }
            }
        }
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
