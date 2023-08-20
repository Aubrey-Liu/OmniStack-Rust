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

    class NodeEventTcpClosed : public Event {
    public:
        NodeEventTcpClosed() : Event(kNodeEventTypeTcpClosed) {}
        NodeEventTcpClosed(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kNodeEventTypeTcpClosed), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}
    
        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
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
        NodeEventUdpClosed(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kNodeEventTypeUdpClosed), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}
    
        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    static_assert(sizeof(NodeEventUdpClosed) <= kEventMaxLength, "NodeEventUdpClosed too large");
}

#endif
