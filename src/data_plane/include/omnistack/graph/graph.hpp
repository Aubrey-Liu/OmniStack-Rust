//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_GRAPH_H
#define OMNISTACK_GRAPH_H

namespace omnistack::data_plane {
    /* basic unit of Graph, all nodes in a SubGraph must on the same CPU core */
    class SubGraph {};

    /* consist of SubGraphs, each SubGraph can run on different CPU cores */
    class Graph {
    public:

    };
}
#endif //OMNISTACK_GRAPH_H
