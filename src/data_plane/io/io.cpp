//
// Created by liuhao on 23-6-10.
//

#include <omnistack/io/io_adapter.hpp>

namespace omnistack::io {


    void BaseIoAdapter::RedirectFlow(packet::Packet* packet) {}
    std::vector<int> BaseIoAdapter::AcquireUsablePortIds() { return {}; }
    std::vector<std::string> BaseIoAdapter::AcquireUsablePortNames() { return {}; }
}
