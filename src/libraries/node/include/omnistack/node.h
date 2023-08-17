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
            kClearNodeInfo,
            kNodeClosed,
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

        class BasicNode {
        public:
            uint64_t id_;
            NodeInfo info_;
            void Write(packet::Packet* packet);
            packet::Packet* Read();

            memory::Pointer<EventNode> enode_;
            memory::Pointer<BasicNode> next_;
        private:
            channel::MultiWriterChannel* channel_;
        };

        /**
         * @brief Every tnode can have only one Enode as target
        */
        // void Connect(BasicNode* basic_node, EventNode* event_node);
        
        // BasicNode* CreateBasicNodeNode();

        // EventNode* CreateEventNode();

        // void StartControlPlane();

        // void InitializeSubsystem();
    }
}

#endif
