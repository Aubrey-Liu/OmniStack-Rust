//
// Created by liuhao on 23-6-10.
//

#include <omnistack/io/io_adapter.hpp>

namespace omnistack::io {
    void IoRecvQueue::RedirectFlow(packet::Packet* packet) {}
    std::vector<int> BaseIoFunction::AcquireUsablePortIds() { return {}; }
    std::vector<std::string> BaseIoFunction::AcquireUsablePortNames() { return {}; }
}
