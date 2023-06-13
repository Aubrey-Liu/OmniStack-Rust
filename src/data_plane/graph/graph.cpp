//
// Created by liuhao on 23-6-3.
//

#include <omnistack/graph/graph.hpp>

namespace omnistack::data_plane {
    Graph::Graph(std::vector<std::string> &&node_names, std::vector<uint32_t> &&node_sub_graph_ids,
                 std::vector<GraphLink> &&links, std::vector<std::vector<uint32_t>> &&mutex_links,
                 std::vector<std::vector<uint32_t>> &&equal_links) : node_names_(node_names), node_sub_graph_ids_(node_sub_graph_ids), links_(links), mutex_links_(mutex_links), equal_links_(equal_links) {
        CreateSubGraphs();
    }

    void Graph::CreateSubGraphs() {
        for(auto p : node_sub_graph_ids_)
            if(sub_graphs_.find(p) == sub_graphs_.end())
                sub_graphs_.emplace(p, SubGraph(*this, p));
    }

    SubGraph::SubGraph(omnistack::data_plane::Graph &graph, uint32_t sub_graph_id) : graph_(graph), sub_graph_id_(sub_graph_id) {
        /* add nodes */
        for(auto p : graph.node_sub_graph_ids_)
            if(p == sub_graph_id)
                node_ids_.push_back(p);

        /* add links */
        for(auto p : graph.links_) {
            auto u = p.first;
            auto v = p.second;
            if(graph.node_sub_graph_ids_[u] != sub_graph_id && graph.node_sub_graph_ids_[v] != sub_graph_id)
                continue;
            if(graph.node_sub_graph_ids_[u] == sub_graph_id && graph.node_sub_graph_ids_[v] == sub_graph_id) {
                if(local_links_.find(u) == local_links_.end()) local_links_.emplace(u, std::set<uint32_t>({v}));
                else local_links_[u].insert(v);
            }
            else {
                if (remote_links_.find(u) == remote_links_.end()) remote_links_.emplace(u, std::set<uint32_t>({v}));
                else remote_links_[u].insert(v);
            }
        }

        /* mark mutex links */
        for(const auto& group : graph.mutex_links_) {
            std::set<uint32_t> mark;
            for (auto idx: group) {
                auto p = graph.links_[idx];
                auto u = p.first;
                auto v = p.second;
                if (mutex_links_.find(u) == mutex_links_.end())
                    mutex_links_.emplace(u, std::vector<std::set<uint32_t>>(1, std::set<uint32_t>({v})));
                else if(mark.find(u) != mark.end())
                    mutex_links_[u].rbegin()->insert(v);
                else {
                    mutex_links_[u].push_back(std::set<uint32_t>({v}));
                    mark.insert(u);
                }
            }
        }

        /* mark equal links */
        for(const auto& group : graph.equal_links_) {
            std::set<uint32_t> mark;
            for (auto idx: group) {
                auto p = graph.links_[idx];
                auto u = p.first;
                auto v = p.second;
                if (equal_links_.find(u) == equal_links_.end())
                    equal_links_.emplace(u, std::vector<std::set<uint32_t>>(1, std::set<uint32_t>({v})));
                else if(mark.find(u) != mark.end())
                    equal_links_[u].rbegin()->insert(v);
                else {
                    equal_links_[u].push_back(std::set<uint32_t>({v}));
                    mark.insert(u);
                }
            }
        }
    }

    SubGraph::SubGraph(omnistack::data_plane::SubGraph&& subgraph) noexcept : graph_(subgraph.graph_), sub_graph_id_(subgraph.sub_graph_id_) {
        node_ids_ = std::move(subgraph.node_ids_);
        local_links_ = std::move(subgraph.local_links_);
        remote_links_ = std::move(subgraph.remote_links_);
        mutex_links_ = std::move(subgraph.mutex_links_);
        equal_links_ = std::move(subgraph.equal_links_);
    }
}
