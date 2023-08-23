#include <omnistack/node/node_common.h>

#include <map>
#include <mutex>

namespace omnistack::data_plane::node_common {
    static std::mutex graph_id_map_mutex;
    static std::map<std::string, int> graph_id_map;
    static int graph_id = 0;

    int GetCurrentGraphId(const std::string& graph_prefix) {
        std::lock_guard<std::mutex> lock(graph_id_map_mutex);
        auto it = graph_id_map.find(graph_prefix);
        if (it == graph_id_map.end()) {
            graph_id_map[graph_prefix] = graph_id ++;
            return graph_id_map[graph_prefix];
        }
        return it->second;
    }
}