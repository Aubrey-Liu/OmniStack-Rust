//
// Created by liuhao on 23-6-10.
//

#include <omnistack/engine/engine.hpp>
#include <omnistack/common/constant.hpp>
#include <omnistack/common/time.hpp>

#include <bit>
#include <csignal>

namespace omnistack::data_plane {
    thread_local volatile bool Engine::stop_ = false;
    thread_local Engine* Engine::current_engine_ = nullptr;

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

    Engine* Engine::Create(EngineCreateInfo& info) {
        auto engine = new Engine();
        engine->Init(*info.sub_graph, info.logic_core, info.name_prefix);
        return engine;
    }

    void Engine::Init(SubGraph &sub_graph, uint32_t core, std::string_view name_prefix) {
        /* set up current engine pointer per thread */
        current_engine_ = this;

        /* TODO: bind to CPU core */

        /* create packet pool */
        packet_pool_ = PacketPool::CreatePacketPool(name_prefix, kDefaultPacketPoolSize);
        
        auto& graph = sub_graph.graph_;
        std::map<uint32_t, uint32_t> global_to_local;

        /* initialize forward structure from graph info */
        {
            packet_queue_.clear();

            /* create modules and init them */
            for(auto idx : sub_graph.node_ids_) {
                auto& module_name = graph.node_names_[idx];
                modules_.push_back(ModuleFactory::instance_().Create(common::Crc32(module_name)));
                uint32_t module_id = modules_.size() - 1;
                module_name_crc32_[module_id] = common::Crc32(module_name);
                global_to_local.emplace(idx, module_id);
                local_to_global.push_back(idx);
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
                            local_to_global.push_back(global_idv);
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
                        local_to_global.push_back(global_idu);
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
                std::vector<std::pair<uint32_t, uint32_t>> upstream_nodes;
                upstream_nodes.reserve(links.size());
                for(auto idx : links)
                    upstream_nodes.emplace_back(module_name_crc32_[idx], local_to_global[idx]);
                modules_[i]->set_upstream_nodes_(upstream_nodes);
            }
        }

        /* initialize module filters */
        {
            for(uint32_t u = 0; u < module_num_; u ++) {
                std::vector<std::pair<uint32_t, uint32_t>> modules;
                std::vector<BaseModule::Filter> filters;
                std::vector<uint32_t> filter_masks;
                std::vector<std::set<uint32_t>> groups;
                std::vector<BaseModule::FilterGroupType> group_types;
                uint32_t assigned_id = 0;

                modules.reserve(downstream_links_[u].size());
                filters.reserve(downstream_links_[u].size());
                filter_masks.reserve(downstream_links_[u].size());

                std::map<uint32_t, uint32_t> local_to_idx;
                for(uint32_t j = 0; j < downstream_links_[u].size(); j ++) {
                    auto downstream_node = downstream_links_[u][j];
                    modules.push_back(std::make_pair(modules_[u]->name_(), local_to_global[downstream_node]));
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

                modules_[u]->RegisterDownstreamFilters(modules, filters, filter_masks, groups, group_types);
            }

        }

        /* set event entry point */
        {
            for(auto& i : modules_) i->set_raise_event_(RaiseEvent);
        }

        /* initialize module */
        {
            for(auto& i : modules_) i->Initialize(name_prefix, packet_pool_);
        }

        /* register events */
        {
            for(auto i = 0; i < modules_.size(); i ++) {
                auto events = modules_[i]->RegisterEvents();
                for(auto event : events) event_entries_[event].push_back(i);
            }
        }

        /* register module timers */
        {
            for(auto i = 0; i < modules_.size(); i ++)
                if(modules_[i]->max_burst_()) 
                    timer_list_.push_back(std::make_pair(i, modules_[i]->max_burst_()));
        }

        /* TODO: initialize channels to remote engine */

        /* register signal handler */
        signal(SIGINT, SigintHandler);
    }

    void Engine::Destroy(Engine* engine) {
        delete engine;
    }

    Engine::~Engine() {
        packet_queue_.clear();

        /* destroy modules */
        for(auto& module : modules_)
            module->Destroy();

        PacketPool::DestroyPacketPool(packet_pool_);

        /* TODO: destroy channels */
    }


    inline void Engine::ForwardPacket(Packet* &packet, uint32_t node_idx) {
        auto forward_mask = packet->next_hop_filter_;
        if(forward_mask == 0) [[unlikely]] {
            packet->Release();
            return;
        }

        uint32_t reference_count = packet->reference_count_ - 1;
        packet->upstream_node_id_ = local_to_global[node_idx];
        packet->upstream_node_name_ = module_name_crc32_[node_idx];
        do [[unlikely]] {
            auto idx = std::countr_zero(forward_mask);
            forward_mask ^= (1 << idx);
            auto downstream_node = downstream_links_[node_idx][idx];

            if(module_read_only_[downstream_node]) {
                packet_queue_.emplace_back(downstream_node, packet);
                reference_count ++;
            }
            else if(downstream_node < module_num_) {
                if(reference_count > 0) [[unlikely]] {
                    /* duplicate the packet and enqueue it */
                    auto packet_copy = packet_pool_->Duplicate(packet);
                    packet_queue_.emplace_back(downstream_node, packet_copy);
                }
                else {
                    packet_queue_.emplace_back(downstream_node, packet);
                    reference_count ++;
                }
            }
            else [[unlikely]] {
                /* TODO: duplicate the packet and send it to remote */
            }
        } while(forward_mask);

        packet->reference_count_ = reference_count;
        packet = packet->next_packet_.Get();
    }

    void Engine::HandleEvent(Event* event) {
        auto& module_ids = event_entries_[event->type_];
        for(auto module_id : module_ids) {
            auto ret = modules_[module_id]->EventCallback(event);
            if(ret != nullptr) {
                if(!ret->next_hop_filter_) ret->next_hop_filter_ = next_hop_filter_default_[module_id];
                modules_[module_id]->ApplyDownstreamFilters(ret);
                ForwardPacket(ret, module_id);
            }
        }
    }

    void Engine::Run() {
        next_hop_filter_default_.resize(module_num_);
        module_read_only_.resize(assigned_module_idx_, false);
        for(int i = 0; i < module_num_; i ++) {
            next_hop_filter_default_[i] = (1 << downstream_links_[i].size()) - 1;
            module_read_only_[i] = modules_[i]->type_() == BaseModule::ModuleType::kReadOnly;
        }

        while(!stop_) {
            /* TODO: receive from remote channels */

            /* handle timer logic */
            uint64_t tick = common::NowUs();
            for(auto [node_idx, burst] : timer_list_) {
                for(auto i = 0; i < burst; i ++) {
                    auto packet = modules_[node_idx]->TimerLogic(tick);
                    /* if multiple packets are returned, they will be in reverse order while applying filters */
                    if(packet != nullptr) [[likely]] {
                        if(!packet->next_hop_filter_) packet->next_hop_filter_ = next_hop_filter_default_[node_idx];
                        modules_[node_idx]->ApplyDownstreamFilters(packet);
                        ForwardPacket(packet, node_idx);
                    }
                    else break;
                    while (packet != nullptr) [[unlikely]] {
                        if(!packet->next_hop_filter_) packet->next_hop_filter_ = next_hop_filter_default_[node_idx];
                        modules_[node_idx]->ApplyDownstreamFilters(packet);
                        ForwardPacket(packet, node_idx);
                    }
                }
            }

            /* process packets in queue */
            while(!packet_queue_.empty()) [[likely]] {
                auto node_idx = packet_queue_.back().first;
                auto packet = packet_queue_.back().second;
                packet_queue_.pop_back();

                packet->next_hop_filter_ = next_hop_filter_default_[node_idx];
                auto return_packet = modules_[node_idx]->MainLogic(packet);
                /* if multiple packets are returned, they will be in reverse order while applying filters */
                if(return_packet != nullptr) [[likely]] {
                    modules_[node_idx]->ApplyDownstreamFilters(return_packet);
                    ForwardPacket(return_packet, node_idx);
                }
                while (return_packet != nullptr) [[unlikely]] {
                    modules_[node_idx]->ApplyDownstreamFilters(return_packet);
                    ForwardPacket(return_packet, node_idx);
                }
            }
        }
    }
}
