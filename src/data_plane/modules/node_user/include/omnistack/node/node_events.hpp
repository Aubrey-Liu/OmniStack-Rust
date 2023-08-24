// Created by JeremyGuo on 23-8-20

#ifndef OMNISTACK_NODE_EVENTS_HPP
#define OMNISTACK_NODE_EVENTS_HPP

#include <omnistack/module/event.hpp>
#include <omnistack/node.h>

namespace omnistack::data_plane::node_common {
    constexpr Event::EventType kNodeEventTypeTcpClosed = Event::GenerateEventType("node.tcp.closed");
    constexpr Event::EventType kNodeEventTypeTcpNewNode = Event::GenerateEventType("node.tcp.new_node");
    constexpr Event::EventType kNodeEventTypeTcpConnect = Event::GenerateEventType("node.tcp.connect");
    constexpr Event::EventType kNodeEventTypeUdpClosed = Event::GenerateEventType("node.udp.closed");
    constexpr Event::EventType kNodeEventTypeAnyInsert = Event::GenerateEventType("node.any.insert");

    class NodeEventTcpClosed : public Event {
    public:
        NodeEventTcpClosed() : Event(kNodeEventTypeTcpClosed) {}
        NodeEventTcpClosed(omnistack::node::BasicNode* node) :
            Event(kNodeEventTypeTcpClosed), node_(node) {}
        
        omnistack::node::BasicNode* node_;
    };
    static_assert(sizeof(NodeEventTcpClosed) <= kEventMaxLength, "NodeEventTcpClosed too large");

    class NodeEventTcpNewNode : public Event {
    public:
        NodeEventTcpNewNode() : Event(kNodeEventTypeTcpNewNode) {}
        NodeEventTcpNewNode(omnistack::node::BasicNode* node) :
            Event(kNodeEventTypeTcpNewNode), node_(node) {}
        
        omnistack::node::BasicNode* node_;
    };
    static_assert(sizeof(NodeEventTcpNewNode) <= kEventMaxLength, "NodeEventTcpNewNode too large");

    class NodeEventTcpConnect : public Event {
    public:
        NodeEventTcpConnect() : Event(kNodeEventTypeTcpConnect) {}
        NodeEventTcpConnect(omnistack::node::BasicNode* node) :
            Event(kNodeEventTypeTcpConnect), node_(node) {}
        
        omnistack::node::BasicNode* node_;
    };
    static_assert(sizeof(NodeEventTcpConnect) <= kEventMaxLength, "NodeEventTcpConnect too large");

    class NodeEventUdpClosed : public Event {
    public:
        NodeEventUdpClosed() : Event(kNodeEventTypeUdpClosed) {}
        NodeEventUdpClosed(omnistack::node::BasicNode* node) :
            Event(kNodeEventTypeUdpClosed), node_(node) {}
        
        omnistack::node::BasicNode* node_;
    };
    static_assert(sizeof(NodeEventUdpClosed) <= kEventMaxLength, "NodeEventUdpClosed too large");

    class NodeEventAnyInsert : public Event {
    public:
        NodeEventAnyInsert() : Event(kNodeEventTypeAnyInsert) {}
        NodeEventAnyInsert(omnistack::node::BasicNode* node) :
            Event(kNodeEventTypeAnyInsert), node_(node) {}
        
        omnistack::node::BasicNode* node_;
    };
    static_assert(sizeof(NodeEventAnyInsert) <= kEventMaxLength, "NodeEventAnyInsert too large");
}

#endif
