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
        void Init(SubGraph& sub_graph, uint32_t core);

        void Run();

        void Destroy();

        void SigintHandler(int sig) { stop_ = true; }

    private:
        uint32_t module_num_;
        std::vector<Module> modules_;
        std::vector<std::vector<uint32_t>> upstream_links_;
        std::vector<std::vector<uint32_t>> downstream_links_;

        std::vector<uint32_t> timer_modules_;
        std::vector<std::pair<Channel, uint32_t>> receive_channels_;
        std::vector<Channel> send_channels_;

        bool stop_ = false;

        bool CompareLinks(uint32_t x, uint32_t y);

        void SortLinks(std::vector<uint32_t>& links);
    };
}

#endif //OMNISTACK_ENGINE_HPP
