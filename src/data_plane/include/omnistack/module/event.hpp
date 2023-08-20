//
// Created by liuhao on 23-8-12.
//

#ifndef OMNISTACK_MODULE_EVENT_HPP
#define OMNISTACK_MODULE_EVENT_HPP

#include <functional>
#include <omnistack/common/hash.hpp>
#include <omnistack/packet/packet.hpp>

namespace omnistack::data_plane {

    constexpr uint32_t kEventMaxLength = 64;

    class Event {
    public:
        typedef uint32_t EventType;

        static consteval EventType GenerateEventType(const char name[]) {
            return common::ConstCrc32(name);
        }

        Event(EventType type) : type_(type) {}
        virtual ~Event() = default;

        EventType type_;
    };

    typedef std::function<packet::Packet*(Event* event)> EventCallback;
    typedef std::pair<Event::EventType, EventCallback> EventRegisterEntry;
}

#endif //OMNISTACK_MODULE_EVENT_HPP
