//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_STEER_INFO_H
#define OMNISTACK_STEER_INFO_H

/* at most 32 outbound-link, use a mask to filter link */
/* also at most 32 inbound-link, mask is used to identify upstream node */
/* the mapping from name to mask will be passed in while initializing */
class SteerInfo {
public:
    uint32_t m_link_filter;
};

#endif //OMNISTACK_STEER_INFO_H
