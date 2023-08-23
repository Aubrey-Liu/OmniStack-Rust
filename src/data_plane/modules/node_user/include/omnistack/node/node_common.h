#ifndef OMNISTACK_NODE_COMMON_H
#define OMNISTACK_NODE_COMMON_H

#include <string>

namespace omnistack::data_plane::node_common {
    int GetCurrentGraphId(const std::string& graph_prefix);
}

#endif
