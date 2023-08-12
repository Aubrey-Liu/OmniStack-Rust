//
// Created by liuhao on 23-8-12.
//

#ifndef OMNISTACK_MODULE_EVENT_HPP
#define OMNISTACK_MODULE_EVENT_HPP

/* TODO: deliver control message to any module by a seperated control channel(event base) */

#include <functional>
#include <omnistack/module/hash.hpp>

namespace omnistack::data_plane {

    class Event {
    public:
        typedef uint32_t EventType;

        static consteval EventType GenerateEventType(const char name[]) {
            return ConstCrc32(name);
        }
    };

    typedef std::function<void(Event* event)> EventCallback;
    typedef std::pair<Event::EventType, EventCallback> EventRegisterEntry;
}

#endif //OMNISTACK_MODULE_EVENT_HPP
