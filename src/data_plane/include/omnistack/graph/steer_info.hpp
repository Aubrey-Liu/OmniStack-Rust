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
            uint32_t path_filter_;     /* bitmask presents all possible path at present */
            uint32_t upstream_node_;   /* identify the upstream node of current packet */
            bool first_hop_;           /* if this packet is at first hop */
        };
    }
}
#endif //OMNISTACK_STEER_INFO_H
