//
// Created by liuhao on 23-8-16.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP
#define OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP

#include <omnistack/module/event.hpp>
#include <omnistack/node.h>

namespace omnistack::data_plane::tcp_common {

    constexpr Event::EventType kTcpEventTypeListen = Event::GenerateEventType("tcp.listen");
    constexpr Event::EventType kTcpEventTypeActiveConnect = Event::GenerateEventType("tcp.active_connect");
    constexpr Event::EventType kTcpEventTypeConnected = Event::GenerateEventType("tcp.connected");
    constexpr Event::EventType kTcpEventTypeActiveClose = Event::GenerateEventType("tcp.active_disconnect");
    constexpr Event::EventType kTcpEventTypePassiveClose = Event::GenerateEventType("tcp.passive_disconnect");
    constexpr Event::EventType kTcpEventTypeClosed = Event::GenerateEventType("tcp.closed");

    class TcpListenOptions {
    public:
        TcpListenOptions() : congestion_control_algorithm_(nullptr) {}
        TcpListenOptions(char* congestion_control_algorithm) : congestion_control_algorithm_(congestion_control_algorithm) {}

        char* congestion_control_algorithm_;
    };

    class TcpEventListen : public Event {
    public:
        TcpEventListen() : Event(kTcpEventTypeListen) {}
        TcpEventListen(uint32_t local_ipv4, uint16_t local_port, TcpListenOptions options, node::BasicNode* node) : Event(kTcpEventTypeListen), local_ipv4_(local_ipv4), local_port_(local_port), options_(options), node_(node) {}

        uint32_t local_ipv4_;
        uint16_t local_port_;
        TcpListenOptions options_;
        node::BasicNode* node_;                // node that this flow belongs to
    };
    static_assert(sizeof(TcpEventListen) <= kEventMaxLength, "TcpEventListen too large");

    class TcpEventActiveConnect : public Event {
    public:
        TcpEventActiveConnect() : Event(kTcpEventTypeActiveConnect) {}
        TcpEventActiveConnect(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port, node::BasicNode* node) :
            Event(kTcpEventTypeActiveConnect), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port), node_(node) {}
    
        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
        node::BasicNode* node_;                // node that this flow belongs to
    };
    static_assert(sizeof(TcpEventActiveConnect) <= kEventMaxLength, "TcpEventActiveConnect too large");

    class TcpEventConnected : public Event {
    public:
        TcpEventConnected() : Event(kTcpEventTypeConnected) {}
        TcpEventConnected(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypeConnected), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}
    
        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    static_assert(sizeof(TcpEventConnected) <= kEventMaxLength, "TcpEventConnected too large");

    class TcpEventActiveClose : public Event {
    public:
        TcpEventActiveClose() : Event(kTcpEventTypeActiveClose) {}
        TcpEventActiveClose(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypeActiveClose), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}

        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    static_assert(sizeof(TcpEventActiveClose) <= kEventMaxLength, "TcpEventActiveClose too large");

    class TcpEventPassiveClose : public Event {
    public:
        TcpEventPassiveClose() : Event(kTcpEventTypePassiveClose) {}
        TcpEventPassiveClose(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypePassiveClose), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}

        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    static_assert(sizeof(TcpEventPassiveClose) <= kEventMaxLength, "TcpEventPassiveClose too large");

    class TcpEventClosed : public Event {
    public:
        TcpEventClosed() : Event(kTcpEventTypeClosed) {}
        TcpEventClosed(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypeClosed), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}

        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    static_assert(sizeof(TcpEventClosed) <= kEventMaxLength, "TcpEventClosed too large");

}

#endif //OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP
