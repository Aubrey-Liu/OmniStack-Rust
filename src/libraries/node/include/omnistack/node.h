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

        class BasicNode {
        public:
            uint64_t id_;
            void Write(packet::Packet* packet);
            packet::Packet* Read();

            memory::Pointer<EventNode> enode_;
        private:
            channel::MultiWriterChannel* channel_;
        };

        /**
         * @brief Every tnode can have only one Enode as target
        */
        void Connect(BasicNode* basic_node, EventNode* event_node);
        
        BasicNode* CreateBasicNodeNode();

        EventNode* CreateEventNode();

        void StartControlPlane();

        void InitializeSubsystem();
    }
}

#endif
