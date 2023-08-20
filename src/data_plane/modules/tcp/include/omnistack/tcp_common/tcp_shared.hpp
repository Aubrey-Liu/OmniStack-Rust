//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
#define OMNISTACK_TCP_COMMON_TCP_SHARED_HPP

#include <omnistack/tcp_common/tcp_state.hpp>
#include <omnistack/tcp_common/tcp_constant.hpp>
#include <omnistack/tcp_common/tcp_events.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/time.hpp>
#include <omnistack/hashtable/hashtable.hpp>
#include <omnistack/module/module.hpp>

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

        TcpListenFlow* GetListenFlow(uint32_t local_ip, uint16_t local_port);

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
            handle->listen_table_ = hashtable::Hashtable::Create(kTcpFlowTableSize, 6);
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
        if(flow == nullptr) [[unlikely]] return nullptr;
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

    inline TcpListenFlow* TcpSharedHandle::GetListenFlow(uint32_t local_ip, uint16_t local_port) {
        static thread_local TcpListenFlow* key;
        key->local_ip_ = local_ip;
        key->local_port_ = local_port;
        return static_cast<TcpListenFlow*>(listen_table_->Lookup(key));
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

    inline void DecodeOptions(TcpHeader* tcp, uint8_t length, uint16_t *mss, uint8_t* wscale, uint8_t* sack_permit, uint32_t* sack_block, uint32_t* timestamp, uint32_t* timestamp_echo) {
        if(length > sizeof(TcpHeader)) [[likely]] {
            uint8_t* options = reinterpret_cast<uint8_t*>(tcp) + sizeof(TcpHeader);
            uint8_t* options_end = reinterpret_cast<uint8_t*>(tcp) + length;
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

    consteval uint8_t TcpHeaderLength(bool mss, bool wsopt, bool sack, bool sack_permitted, bool tspot) {
        uint8_t length = sizeof(TcpHeader);
#if defined (OMNI_TCP_OPTION_MSS)
        if(mss) length = length + ((TCP_OPTION_LENGTH_MSS + 3) >> 2 << 2);
#endif
#if defined (OMNI_TCP_OPTION_WSOPT)
        if(wsopt) length = length + ((TCP_OPTION_LENGTH_WSOPT + 3) >> 2 << 2);
#endif
#if defined (OMNI_TCP_OPTION_SACK)
        if(sack) length = length + ((TCP_OPTION_LENGTH_SACK + 3) >> 2 << 2);
#endif
#if defined (OMNI_TCP_OPTION_SACK_PERMITTED)
        if(sack_permitted) length = length + ((TCP_OPTION_LENGTH_SACK_PERMITTED + 3) >> 2 << 2);
#endif
#if defined (OMNI_TCP_OPTION_TSPOT)
        if(tspot) length = length + ((TCP_OPTION_LENGTH_TSPOT + 3) >> 2 << 2);
#endif
        return length + 3 >> 2 << 2;
    }

    /**
     * @brief Build a tcp packet with the given tcp flags.
     * @note this function will not increase send_nxt_.
    */
    inline Packet* BuildReplyPacket(TcpFlow* flow, uint16_t tcp_flags, PacketPool* packet_pool) {
        auto packet = packet_pool->Allocate();

        /* build tcp header */
        auto& header_tcp = packet->packet_headers_[packet->header_tail_++];
        header_tcp.length_ = TcpHeaderLength(false, false, false, false, true);
        header_tcp.offset_ = 0;
        packet->data_ = packet->data_ - header_tcp.length_;
        auto tcp = reinterpret_cast<TcpHeader*>(packet->data_ + header_tcp.offset_);
        tcp->sport = flow->local_port_;
        tcp->dport = flow->remote_port_;
        tcp->seq = htonl(flow->send_variables_.send_nxt_);
        tcp->ACK = htonl(flow->receive_variables_.recv_nxt_);
        tcp->tcpflags = tcp_flags;
        tcp->dataofs = header_tcp.length_ >> 2;
        tcp->window = htons(flow->receive_variables_.recv_wnd_);
        tcp->urgptr = 0;

        /* set tcp options */
        auto tcp_options = reinterpret_cast<uint8_t*>(tcp) + sizeof(TcpHeader);
#if defined (OMNI_TCP_OPTION_TSPOT)
        if(flow->receive_variables_.timestamp_recent_ != 0) {
            *tcp_options = TCP_OPTION_KIND_NOP;
            *(tcp_options + 1) = TCP_OPTION_KIND_NOP;
            *(tcp_options + 2) = TCP_OPTION_KIND_TSPOT;
            *(tcp_options + 3) = TCP_OPTION_LENGTH_TSPOT;
            *reinterpret_cast<uint32_t*>(tcp_options + 4) = htonl(static_cast<uint32_t>(NowMs()));
            *reinterpret_cast<uint32_t*>(tcp_options + 8) = htonl(flow->receive_variables_.timestamp_recent_);
            tcp_options = tcp_options + 12;
        }
#endif

        /* build ipv4 header */
        auto& header_ipv4 = packet->packet_headers_[packet->header_tail_++];
        header_ipv4.length_ = sizeof(Ipv4Header);
        header_ipv4.offset_ = 0;
        header_tcp.offset_ = header_tcp.offset_ + header_ipv4.length_;
        packet->data_ = packet->data_ - header_ipv4.length_;
        auto ipv4 = reinterpret_cast<Ipv4Header*>(packet->data_ + header_ipv4.offset_);
        ipv4->version = 4;
        ipv4->proto = IP_PROTO_TYPE_TCP;
        ipv4->src = flow->local_ip_;
        ipv4->dst = flow->remote_ip_;

        return packet;
    }

    /**
     * @brief Build a tcp packet with the given tcp flags.
     * @note this function will not increase send_nxt_.
    */
    inline Packet* BuildReplyPacketWithFullOptions(TcpFlow* flow, uint16_t tcp_flags, PacketPool* packet_pool) {
        auto packet = packet_pool->Allocate();

        /* build tcp header */
        auto& header_tcp = packet->packet_headers_[packet->header_tail_++];
        header_tcp.length_ = TcpHeaderLength(true, true, true, true, true);
        header_tcp.offset_ = 0;
        packet->data_ = packet->data_ - header_tcp.length_;
        auto tcp = reinterpret_cast<TcpHeader*>(packet->data_ + header_tcp.offset_);
        tcp->sport = flow->local_port_;
        tcp->dport = flow->remote_port_;
        tcp->seq = htonl(flow->send_variables_.send_nxt_);
        tcp->ACK = htonl(flow->receive_variables_.recv_nxt_);
        tcp->tcpflags = tcp_flags;
        tcp->dataofs = header_tcp.length_ >> 2;
        tcp->window = htons(flow->receive_variables_.recv_wnd_);
        tcp->urgptr = 0;

        /* set tcp options */
        auto tcp_options = reinterpret_cast<uint8_t*>(tcp) + sizeof(TcpHeader);
#if defined (OMNI_TCP_OPTION_MSS)
        if(flow->mss_ != 0) {
            *tcp_options = TCP_OPTION_KIND_MSS;
            *(tcp_options + 1) = TCP_OPTION_LENGTH_MSS;
            *reinterpret_cast<uint16_t*>(tcp_options + 2) = htons(flow->mss_);
            tcp_options = tcp_options + 4;
        }
#endif
#if defined (OMNI_TCP_OPTION_WSOPT)
        if(flow->receive_variables_.recv_wscale_ != 0) {
            *tcp_options = TCP_OPTION_KIND_NOP;
            *(tcp_options + 1) = TCP_OPTION_KIND_WSOPT;
            *(tcp_options + 2) = TCP_OPTION_LENGTH_WSOPT;
            *(tcp_options + 3) = flow->receive_variables_.recv_wscale_;
            tcp_options = tcp_options + 4;
        }
#endif
#if defined (OMNI_TCP_OPTION_TSPOT)
        if(flow->receive_variables_.timestamp_recent_ != 0) {
            *tcp_options = TCP_OPTION_KIND_NOP;
            *(tcp_options + 1) = TCP_OPTION_KIND_NOP;
            *(tcp_options + 2) = TCP_OPTION_KIND_TSPOT;
            *(tcp_options + 3) = TCP_OPTION_LENGTH_TSPOT;
            *reinterpret_cast<uint32_t*>(tcp_options + 4) = htonl(static_cast<uint32_t>(NowMs()));
            *reinterpret_cast<uint32_t*>(tcp_options + 8) = htonl(flow->receive_variables_.timestamp_recent_);
            tcp_options = tcp_options + 12;
        }
#endif

        /* build ipv4 header */
        auto& header_ipv4 = packet->packet_headers_[packet->header_tail_++];
        header_ipv4.length_ = sizeof(Ipv4Header);
        header_ipv4.offset_ = 0;
        header_tcp.offset_ = header_tcp.offset_ + header_ipv4.length_;
        packet->data_ = packet->data_ - header_ipv4.length_;
        auto ipv4 = reinterpret_cast<Ipv4Header*>(packet->data_ + header_ipv4.offset_);
        ipv4->version = 4;
        ipv4->proto = IP_PROTO_TYPE_TCP;
        ipv4->src = flow->local_ip_;
        ipv4->dst = flow->remote_ip_;

        return packet;
    }

}

#endif //OMNISTACK_TCP_COMMON_TCP_SHARED_HPP
