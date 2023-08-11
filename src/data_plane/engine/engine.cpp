//
// Created by liuhao on 23-6-10.
//

#include <omnistack/engine/engine.hpp>

#include <bit>
#include <csignal>

namespace omnistack::data_plane {
    thread_local volatile bool Engine::stop_ = false;

    bool Engine::CompareLinks(uint32_t x, uint32_t y) {
        int t1 = x < module_num_;
        int t2 = y < module_num_;
        if(t1 != t2) return t1 < t2;
        if(modules_[x]->type_() == BaseModule::ModuleType::kReadOnly) return true;
        return modules_[y]->type_() != BaseModule::ModuleType::kReadOnly;
    }

    void Engine::SortLinks(std::vector<uint32_t> &links) {
        for(int i = 0; i < links.size(); i ++)
            for(int j = 0; j < links.size() - 1; j ++)
                if(!CompareLinks(links[j], links[j + 1]))
                    std::swap(links[j], links[j + 1]);
                else break;
    }

    void Engine::Init(SubGraph &sub_graph, uint32_t core, std::string_view name_prefix) {
        /* TODO: create packet pool */
        PacketPool* packet_pool;
        auto& graph = sub_graph.graph_;
        std::map<uint32_t, uint32_t> global_to_local;

        /* initialize forward structure from graph info */
        {
            /* create modules and init them */
            for(auto idx : sub_graph.node_ids_) {
                auto& module_name = graph.node_names_[idx];
                modules_.push_back(ModuleFactory::instance_().Create(module_name));
                uint32_t module_id = modules_.size() - 1;
                global_to_local.emplace(idx, module_id);
                local_to_global.emplace(module_id, idx);
                modules_.at(module_id)->Initialize(name_prefix, packet_pool);
            }

            module_num_ = modules_.size();
            upstream_links_.resize(module_num_);
            downstream_links_.resize(module_num_);

            for(auto& link : sub_graph.local_links_) {
                auto global_idu = link.first;
                for(auto global_idv : link.second) {
                    auto idu = global_to_local.at(global_idu);
                    auto idv = global_to_local.at(global_idv);
                    downstream_links_[idu].push_back(idv);
                    upstream_links_[idv].push_back(idu);
                }
            }

            assigned_module_idx_ = module_num_;
            for(auto& link : sub_graph.remote_links_) {
                auto global_idu = link.first;
                if(graph.node_sub_graph_ids_[global_idu] == sub_graph.sub_graph_id_) {
                    for(auto global_idv : link.second) {
                        auto idu = global_to_local.at(global_idu);
                        uint32_t idv;
                        if (global_to_local.find(global_idv) != global_to_local.end())
                            idv = global_to_local.at(global_idv);
                        else {
                            idv = assigned_module_idx_ ++;
                            global_to_local.emplace(global_idv, idv);
                            local_to_global.emplace(idv, global_idv);
                        }
                        downstream_links_[idu].push_back(idv);
                    }
                }
                else {
                    uint32_t idu;
                    if(global_to_local.find(global_idu) != global_to_local.end())
                        idu = global_to_local.at(global_idu);
                    else {
                        idu = assigned_module_idx_ ++;
                        global_to_local.emplace(global_idu, idu);
                        local_to_global.emplace(idu, global_idu);
                    }
                    for(auto global_idv : link.second) {
                        auto idv = global_to_local.at(global_idv);
                        upstream_links_[idv].push_back(idu);
                    }
                }
            }

            for(auto& links : downstream_links_)
                SortLinks(links);
            for(uint32_t i = 0; i < module_num_; i ++) {
                auto& links = upstream_links_[i];
                SortLinks(links);
                std::vector<std::pair<std::string, uint32_t>> upstream_nodes;
                upstream_nodes.reserve(links.size());
                for(auto idx : links)
                    upstream_nodes.emplace_back(graph.node_names_[local_to_global[idx]], local_to_global[idx]);
                modules_[i]->set_upstream_nodes_(upstream_nodes);
            }
        }

        /* initialize module filters */
        {
            for(uint32_t u = 0; u < module_num_; u ++) {
                std::vector<BaseModule::Filter> filters;
                std::vector<uint32_t> filter_masks;
                std::vector<std::set<uint32_t>> groups;
                std::vector<BaseModule::FilterGroupType> group_types;
                uint32_t assigned_id = 0;

                filters.reserve(downstream_links_[u].size());
                filter_masks.reserve(downstream_links_[u].size());

                std::map<uint32_t, uint32_t> local_to_idx;
                for(uint32_t j = 0; j < downstream_links_[u].size(); j ++) {
                    auto downstream_node = downstream_links_[u][j];
                    filters.push_back(modules_[downstream_node]->GetFilter(modules_[u]->name_(), local_to_global[downstream_node]));
                    filter_masks.push_back(1 << j);
                    local_to_idx.emplace(downstream_node, j);
                }

                auto global_idu = local_to_global[u];

                auto& mutex_links = sub_graph.mutex_links_;
                if(mutex_links.find(global_idu) != mutex_links.end())
                    for(auto& mutex_group : mutex_links[global_idu]) {
                        groups.emplace_back();
                        group_types.push_back(BaseModule::FilterGroupType::kMutex);
                        for(auto global_idv : mutex_group) {
                            auto v = global_to_local[global_idv];
                            groups.rbegin()->insert(local_to_idx[v]);
                        }
                        assigned_id ++;
                    }

                auto& equal_links = sub_graph.equal_links_;
                if(equal_links.find(global_idu) != equal_links.end())
                    for(auto& equal_group : equal_links[global_idu]) {
                        groups.emplace_back();
                        group_types.push_back(BaseModule::FilterGroupType::kEqual);
                        for(auto global_idv : equal_group) {
                            auto v = global_to_local[global_idv];
                            groups.rbegin()->insert(local_to_idx[v]);
                        }
                        assigned_id ++;
                    }

                modules_[u]->RegisterDownstreamFilters(filters, filter_masks, groups, group_types);
            }
        }

        /* TODO: initialize channels to remote engine */

        /* TODO: register module timers */

        /* register signal handler */
        signal(SIGINT, SigintHandler);
    }

    void Engine::Destroy() {
        /* destroy modules */
        for(auto& module : modules_)
            module->Destroy();

        /* TODO: destroy channels */
    }


    inline void Engine::ForwardPacket(std::vector<QueueItem>& packet_queue, Packet* &packet, uint32_t node_idx) {
        auto forward_mask = packet->next_hop_filter_;
        if(forward_mask == 0) [[unlikely]] {
            packet->Release();
            return;
        }

        uint32_t reference_count = packet->reference_count_ - 1;
        packet->upstream_node_ = local_to_global[node_idx];
        do [[unlikely]] {
            auto idx = std::countr_zero(forward_mask);
            forward_mask ^= (1 << idx);
            auto downstream_node = downstream_links_[node_idx][idx];

            if(module_read_only_[downstream_node]) {
                packet_queue.emplace_back(downstream_node, packet);
                reference_count ++;
            }
            else if(downstream_node < module_num_) {
                if(reference_count > 0) [[unlikely]] {
                    /* TODO: duplicate the packet and enqueue it */
                }
                else {
                    packet_queue.emplace_back(downstream_node, packet);
                    reference_count ++;
                }
            }
            else [[unlikely]] {
                /* TODO: duplicate the packet and send it to remote */
            }
        } while(forward_mask);

        packet->reference_count_ = reference_count;
        packet = packet->next_packet_;
    }

    void Engine::Run() {
        std::vector<QueueItem> packet_queue;

        next_hop_filter_default_.resize(module_num_);
        module_read_only_.resize(assigned_module_idx_, false);
        for(int i = 0; i < module_num_; i ++) {
            next_hop_filter_default_[i] = (1 << downstream_links_[i].size()) - 1;
            module_read_only_[i] = modules_[i]->type_() == BaseModule::ModuleType::kReadOnly;
        }

        uint8_t batch_num = 0;
        while((batch_num ++) || (!stop_)) {
            /* TODO: receive from remote channels */

            /* TODO: handle timer logic */

            /* process packets in queue */
            while(!packet_queue.empty()) [[likely]] {
                auto node_idx = packet_queue.back().first;
                auto packet = packet_queue.back().second;
                packet_queue.pop_back();

                packet->next_hop_filter_ = next_hop_filter_default_[node_idx];
                auto return_packet = modules_[node_idx]->MainLogic(packet);
                if(return_packet != nullptr) [[likely]] {
                    modules_[node_idx]->ApplyDownstreamFilters(return_packet);
                    ForwardPacket(packet_queue, return_packet, node_idx);
                }
                while (return_packet != nullptr) [[unlikely]] {
                    modules_[node_idx]->ApplyDownstreamFilters(return_packet);
                    ForwardPacket(packet_queue, return_packet, node_idx);
                }
            }
        }
    }
}
