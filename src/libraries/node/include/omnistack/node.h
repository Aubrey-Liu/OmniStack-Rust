#ifndef OMNISTACK_NODE_H
#define OMNISTACK_NODE_H

#include <omnistack/channel/channel.h>
#include <omnistack/packet/packet.hpp>
#include <omnistack/memory/memory.h>
#include <omnistack/common/logger.h>
#include <omnistack/common/hash.hpp>
#include <arpa/inet.h>

namespace omnistack {
    namespace node {
        class EventNode {
        public:
            void Write(uint64_t tnode_id);
            uint64_t Read();
            void Init();

            inline memory::Pointer<EventNode> GetNext() {
                return next_;
            }

            inline void SetNext(const memory::Pointer<EventNode>& next) {
                next_ = next;
            }
        private:
            memory::Pointer<channel::MultiWriterChannel> channel_;
            memory::Pointer<EventNode> next_;
        };

        enum class NodeCommandType {
            kUpdateNodeInfo = 0,
            kClearNodeInfo, // => Node Closed
            kPacket
        };

        enum class TransportLayerType {
            kTCP = 0,
            kUDP
        };

        enum class NetworkLayerType {
            kIPv4 = 0,
            kIPv6
        };

        struct NodeInfo {
            union {
                struct {
                    uint32_t sip;
                    uint32_t dip;
                    uint32_t padding[6];
                } ipv4;
                struct {
                    __uint128_t sip;
                    __uint128_t dip;
                } ipv6;
                inline void Set(const uint32_t& sip_, const uint32_t& dip_) {
                    ipv4.sip = sip_;
                    ipv4.dip = dip_;
                    ipv4.padding[0] = 0;
                    ipv4.padding[1] = 0;
                    ipv6.dip = 0;
                }
                inline void Set(const __uint128_t& sip_, const __uint128_t& dip_) {
                    ipv6.sip = sip_;
                    ipv6.dip = dip_;
                }
            } network;
            TransportLayerType transport_layer_type;
            NetworkLayerType network_layer_type;
            union {
                struct {
                    uint16_t sport;
                    uint16_t dport;
                } tcp;
                struct {
                    uint16_t sport;
                    uint16_t dport;
                } udp;
                struct {
                    uint16_t sport;
                    uint16_t dport;
                };
            } transport;
            static_assert(sizeof(transport) == 4);
            static_assert(sizeof(network) == 32);
            static_assert(sizeof(TransportLayerType) == 4);
            static_assert(sizeof(NetworkLayerType) == 4);
            uint32_t padding = 0;

            inline
            uint32_t GetHash() {
                // constexpr int raw_length = sizeof(NodeInfo) / sizeof(uint32_t);
                char* raw_data = (char*)this;
                // uint32_t ret = 0;
                // for (int i = 0; i < raw_length; i ++)
                //     ret = (ret << 1) ^ (raw_data[i]) ^ (raw_data[i] >> 3) ^ ret;
                return common::Crc32(raw_data, sizeof(NodeInfo));
            }

            inline
            void PrintWith(common::LogLevel log_level = common::kInfo) {
                OMNI_LOG_TAG(log_level, "NodeInfo") << "NodeInfo: "
                    << "transport_layer_type = " << (int)transport_layer_type
                    << "network_layer_type = " << (int)network_layer_type
                    << "sip = " << inet_ntoa(*(in_addr*)&network.ipv4.sip)
                    << "dip = " << inet_ntoa(*(in_addr*)&network.ipv4.dip)
                    << "sport = " << ntohs(transport.tcp.sport)
                    << "dport = " << ntohs(transport.tcp.dport);
            }
        };
        static_assert(sizeof(NodeInfo) == 48);

        struct NodeCommandHeader {
            NodeCommandType type;

            char padding[16 - sizeof(NodeCommandType)];
        };

        /** Basic Node's lock is guaranteed by its Channel **/
        class BasicNode {
        public:
            uint32_t com_user_id_;
            NodeInfo info_;
            bool in_hashtable_;
            bool peer_closed_;

            uint32_t user_proc_ref_;
            pthread_spinlock_t user_proc_ref_lock_;

            uint8_t graph_usable_[128];
            int num_graph_usable_;
            
            void Write(packet::Packet* packet);
            packet::Packet* Read();
            void Flush();

            void WriteMulti(packet::Packet* packet);
            packet::Packet* ReadMulti();
            void FlushMulti();

            void WriteBottom(packet::Packet* packet);

            bool IsReadable();
            void UpdateInfo(NodeInfo info);
            void Init();
            void CleanUp();
            void Connect(EventNode* event_node);
            void PutIntoHashtable();
            void ClearFromHashtableAndClose();
            int OpenRef();
            void CloseRef();

            inline memory::Pointer<BasicNode> GetNext() {
                return next_;
            }

            inline void SetNext(const memory::Pointer<BasicNode>& next) {
                next_ = next;
            }
            
            inline static thread_local packet::PacketPool* packet_pool_ = nullptr;

        private:
            memory::Pointer<EventNode> enode_;
            memory::Pointer<BasicNode> next_;
            memory::Pointer<channel::Channel> channel_;
            memory::Pointer<channel::MultiWriterChannel> mw_channel_;

        public:
            struct {
                char padding[common::kCacheLineSize];
                bool in_flush_queue_;
            } node_user_info_;
        };

        /**
         * @brief Every tnode can have only one Enode as target
        */
        BasicNode* CreateBasicNode(uint32_t com_user_id);

        void FlushBottom();

        EventNode* CreateEventNode();

        void ReleaseBasicNode(BasicNode* node);

        void StartControlPlane(int num_com_user);

        void InitializeSubsystem();

        int GetNumNodeUser();
    }
}

#endif
