//
// Created by liuhao on 23-8-16.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP
#define OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP

#include <omnistack/module/event.hpp>

namespace omnistack::data_plane::tcp_common {

    constexpr Event::EventType kTcpEventTypeConnect = Event::GenerateEventType("tcp.connect");
    constexpr Event::EventType kTcpEventTypeDisconnect = Event::GenerateEventType("tcp.disconnect");

    class TcpEventConnect : public Event {
    public:
        TcpEventConnect() : Event(kTcpEventTypeConnect) {}
        TcpEventConnect(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypeConnect), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}
    
        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };

    static_assert(sizeof(TcpEventConnect) <= kEventMaxLength, "TcpEventConnect too large");

    class TcpEventDisconnect : public Event {
    public:
        TcpEventDisconnect() : Event(kTcpEventTypeDisconnect) {}
        TcpEventDisconnect(uint32_t local_ipv4, uint32_t remote_ipv4, uint16_t local_port, uint16_t remote_port) :
            Event(kTcpEventTypeDisconnect), local_ipv4_(local_ipv4), remote_ipv4_(remote_ipv4), local_port_(local_port), remote_port_(remote_port) {}

        uint32_t local_ipv4_;
        uint32_t remote_ipv4_;
        uint16_t local_port_;
        uint16_t remote_port_;
    };
    
    static_assert(sizeof(TcpEventDisconnect) <= kEventMaxLength, "TcpEventDisconnect too large");
}

#endif //OMNISTACK_TCP_COMMON_TCP_EVENTS_HPP
