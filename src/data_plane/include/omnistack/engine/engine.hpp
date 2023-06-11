//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_ENGINE_H
#define OMNISTACK_ENGINE_H

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

        void SigintHandler(int sig) { stop = true; }

    private:
        uint32_t module_num;
        std::vector<Module> modules_;
        std::vector<std::vector<uint32_t>> upstream_links_;
        std::vector<std::vector<uint32_t>> downstream_links_;

        std::vector<uint32_t> timer_modules_;
        std::vector<std::pair<Channel, uint32_t>> receive_channels_;
        std::vector<Channel> send_channels_;

        bool stop = false;

        friend bool compare_link(uint32_t x, uint32_t y);
    };
}

#endif //OMNISTACK_ENGINE_H
