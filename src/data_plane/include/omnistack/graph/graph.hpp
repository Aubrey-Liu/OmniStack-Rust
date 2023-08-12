//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_GRAPH_GRAPH_H
#define OMNISTACK_GRAPH_GRAPH_H

#include <vector>
#include <cstdint>
#include <string>
#include <map>
#include <set>

namespace omnistack::data_plane {
    class Graph;

    /* Constructed by Graph, all nodes in a SubGraph must on the same CPU core */
    class SubGraph {
    public:
        SubGraph(const SubGraph&) = delete;
        SubGraph(SubGraph&&) noexcept;

        const Graph& graph_;
        uint32_t sub_graph_id_;
        std::vector<uint32_t> node_ids_;
        std::map<uint32_t, std::set<uint32_t>> local_links_;
        std::map<uint32_t, std::set<uint32_t>> remote_links_;
        std::map<uint32_t, std::vector<std::set<uint32_t>>> mutex_links_;
        std::map<uint32_t, std::vector<std::set<uint32_t>>> equal_links_;

    private:
        friend class Graph;

        SubGraph(Graph& graph, uint32_t sub_graph_id);
    };

    /* interface for Control Plane, must consist of SubGraphs, each SubGraph can run on different CPU cores */
    class Graph {
    public:
        typedef std::pair<uint32_t, uint32_t> Link;

        Graph(std::vector<std::string>&& node_names, std::vector<uint32_t>&& node_sub_graph_ids,
              std::vector<Link>&& links, std::vector<std::vector<uint32_t>>&& mutex_links,
              std::vector<std::vector<uint32_t>>&& equal_links);
        Graph(const Graph&) = delete;

        SubGraph& sub_graph(uint32_t idx) { return sub_graphs_.at(idx); }

        std::vector<std::string> node_names_;
        std::vector<uint32_t> node_sub_graph_ids_;
        std::vector<Link> links_;
        std::vector<std::vector<uint32_t>> mutex_links_;
        std::vector<std::vector<uint32_t>> equal_links_;
    private:
        void CreateSubGraphs();

        std::map<uint32_t, SubGraph> sub_graphs_;
    };
}
#endif //OMNISTACK_GRAPH_GRAPH_H
