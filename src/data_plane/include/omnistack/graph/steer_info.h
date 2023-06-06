//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_STEER_INFO_H
#define OMNISTACK_STEER_INFO_H

#include <cstdint>

namespace omnistack {
    namespace data_plane {
        class DataPlanePacket : public Packet {
        public:
            uint32_t m_path_filter;     /* bitmask presents all possible path at present */
            uint32_t m_upstream_node;   /* identify the upstream node of current packet */
            bool m_first_hop;           /* if this packet is at first hop */
        };
    }
}
#endif //OMNISTACK_STEER_INFO_H
