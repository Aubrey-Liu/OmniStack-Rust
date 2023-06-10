//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_STEER_INFO_H
#define OMNISTACK_STEER_INFO_H

#include <cstdint>
#include <omnistack/common/packet.hpp>

namespace omnistack {
    namespace data_plane {
        class DataPlanePacket : public Packet {
        public:
            uint32_t next_hop_filter_;      /* bitmask presents all possible next hop nodes at present */
            uint32_t upstream_node_;        /* identify the upstream node of current packet */
        };
    }
}
#endif //OMNISTACK_STEER_INFO_H
