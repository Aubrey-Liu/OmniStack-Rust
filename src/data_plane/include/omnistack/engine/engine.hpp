//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_ENGINE_HPP
#define OMNISTACK_ENGINE_HPP

#include <omnistack/graph/graph.hpp>
#include <omnistack/module/module.hpp>

class Channel;

namespace omnistack::data_plane {
    /* Engine receives a SubGraph and runs it */
    class Engine {
    public:
        void Init(SubGraph& sub_graph, uint32_t core, const std::string& name_prefix);

        void Run();

        void Destroy();

        static void SigintHandler(int sig) { stop_ = true; }

    private:
        typedef std::pair<uint32_t, DataPlanePacket*> QueueItem;

        /* graph info */
        uint32_t module_num_;
        uint32_t assigned_module_idx_;
        std::vector<std::unique_ptr<BaseModule>> modules_;
        std::vector<std::vector<uint32_t>> upstream_links_;
        std::vector<std::vector<uint32_t>> downstream_links_;
        std::map<uint32_t, uint32_t> local_to_global;
        std::vector<uint32_t> timer_modules_;
        std::vector<std::pair<Channel, uint32_t>> receive_channels_;
        std::vector<Channel> send_channels_;


        /* helper structures */
        std::vector<uint32_t> next_hop_filter_default_;
        std::vector<bool> module_read_only_;

        static thread_local volatile bool stop_;

        void ForwardPacket(std::vector<QueueItem>& packet_queue, DataPlanePacket* &packet, uint32_t node_idx);

        bool CompareLinks(uint32_t x, uint32_t y);

        void SortLinks(std::vector<uint32_t>& links);
    };
}

#endif //OMNISTACK_ENGINE_HPP
