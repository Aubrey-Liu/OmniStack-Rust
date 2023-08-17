#ifndef OMNISTACK_NODE_H
#define OMNISTACK_NODE_H

#include <omnistack/channel/channel.h>
#include <omnistack/packet/packet.hpp>
#include <omnistack/memory/memory.h>

namespace omnistack {
    namespace node {
        class EventNode {
        public:
            uint64_t id_;
            void Write(uint64_t tnode_id);
            uint64_t Read();
        private:
            channel::MultiWriterChannel* channel;
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
            TransportLayerType transport_layer_type;
            NetworkLayerType network_layer_type;
            struct {
                struct {
                    uint16_t sport;
                    uint16_t dport;
                } tcp;
                struct {
                    uint16_t sport;
                    uint16_t dport;
                } udp;
            } transport;
            struct {
                struct {
                    uint32_t sip;
                    uint32_t dip;
                } ipv4;
                struct {
                    __uint128_t sip;
                    __uint128_t dip;
                } ipv6;
            } network;
        };

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
            
            void Write(packet::Packet* packet);
            packet::Packet* Read();

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

        private:
            memory::Pointer<EventNode> enode_;
            memory::Pointer<BasicNode> next_;
            memory::Pointer<channel::Channel> channel_;
        };

        /**
         * @brief Every tnode can have only one Enode as target
        */
        // void Connect(BasicNode* basic_node, EventNode* event_node);
        
        BasicNode* CreateBasicNode(uint32_t com_user_id);

        // EventNode* CreateEventNode();

        // void StartControlPlane();

        // void InitializeSubsystem();
    }
}

#endif
