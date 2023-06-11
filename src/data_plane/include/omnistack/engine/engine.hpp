//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_ENGINE_H
#define OMNISTACK_ENGINE_H

#include <omnistack/graph/graph.hpp>

namespace omnistack::data_plane {
    /* Engine receives a SubGraph and runs it */
    class Engine {
    public:
        void Init(Graph& graph, uint32_t sub_graph_id, uint32_t core);

        void Run();

        void Destroy();

    private:
        
    };
}

#endif //OMNISTACK_ENGINE_H
