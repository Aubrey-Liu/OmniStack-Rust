//
// Created by liuhao on 23-8-10.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP
#define OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP

#include <cstdint>

namespace omnistack::data_plane::tcp_common {
    constexpr uint32_t kTcpMaxFlowCount = 65536;
    constexpr uint32_t kTcpFlowTableSize = kTcpMaxFlowCount * 2;

    constexpr uint16_t kTcpMaxSegmentSize = 1460;
    constexpr uint16_t kTcpDefaultSendWindow = 65535;
    constexpr uint16_t kTcpDefaultReceiveWindow = 65535;
    constexpr uint8_t kTcpDefaultReceiveWindowScale = 7;
    constexpr uint64_t kTcpMaximumRetransmissionTimeout = 100000000;
    constexpr uint64_t kTcpMinimumRetransmissionTimeout = 200000;
    constexpr uint64_t kTcpTimeWaitTimeout = 120000000;

    constexpr char kTcpDefaultCongestionControlAlgorithm[] = "Cubic";
}

#endif //OMNISTACK_TCP_COMMON_TCP_CONSTANT_HPP
