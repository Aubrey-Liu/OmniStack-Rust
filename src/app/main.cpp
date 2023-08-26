#include <omnistack/common/config.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <omnistack/node.h>
#include <omnistack/engine/engine.hpp>

namespace omnistack {

    static inline void InitializeMemory() {
#if defined (OMNIMEM_BACKEND_DPDK)
        memory::StartControlPlane(true);
#else
        memory::StartControlPlane();
#endif
        memory::InitializeSubsystem();
        memory::InitializeSubsystemThread();
    }

    static inline void InitializeToken() {
        token::StartControlPlane();
        token::InitializeSubsystem();
    }

    static inline void InitializeChannel() {
        channel::StartControlPlane();
        channel::InitializeSubsystem();
    }

    static inline void InitializeNode() {
        node::StartControlPlane(node::GetNumNodeUser());
        node::InitializeSubsystem();
    }

    static inline data_plane::Graph* CreateGraph(const config::GraphConfig& graph_config) {
        std::vector<std::string> node_names;
        std::vector<uint32_t> node_sub_graph_ids;
        std::vector<data_plane::Graph::Link> links;
        std::vector<std::vector<uint32_t>> mutex_links;
        std::vector<std::vector<uint32_t>> equal_links;

        node_names = graph_config.GetModules();
        node_sub_graph_ids.resize(node_names.size(), 0);
        auto links_config = graph_config.GetLinks();
        for(auto& link_config : links_config) {
            auto& [src, dst] = link_config;
            auto src_idx = std::find(node_names.begin(), node_names.end(), src) - node_names.begin();
            auto dst_idx = std::find(node_names.begin(), node_names.end(), dst) - node_names.begin();
            links.emplace_back(std::make_pair(src_idx, dst_idx));
        }
        mutex_links = graph_config.GetGroups();

        auto graph = new data_plane::Graph(std::move(node_names), std::move(node_sub_graph_ids), std::move(links),
                                           std::move(mutex_links), std::move(equal_links));
        return graph;
    }

    static int EngineThreadEntry(void* arg) {
        auto create_info = reinterpret_cast<data_plane::EngineCreateInfo*>(arg);
        auto engine = data_plane::Engine::Create(*create_info);
        engine->Run();
        data_plane::Engine::Destroy(engine);
        return 0;
    }

}

int main(int argc, char **argv) {
    /* 1. load configurations */
    omnistack::config::ConfigManager::LoadFromDirectory("config");
    omnistack::config::ConfigManager::LoadFromDirectory("graph_config");
    std::string config_name = argc > 1 ? argv[1] : "config";
    auto stack_config = omnistack::config::ConfigManager::GetStackConfig(config_name);

    /* 2. init libraries */
    omnistack::InitializeMemory();
    omnistack::InitializeToken();
    omnistack::InitializeChannel();
    // omnistack::InitializeNode();

    /* 3. create graphs, directly using graph config at present, each graph has only one subgraph */
    std::vector<omnistack::data_plane::Graph*> graphs;
    std::vector<std::string_view> graph_names;
    std::vector<omnistack::data_plane::SubGraph*> sub_graphs;
    std::vector<int> sub_graph_cpus;
    auto graph_entries = stack_config.GetGraphEntries();
    for(auto& graph_entry : graph_entries) {
        auto graph_config = omnistack::config::ConfigManager::GetGraphConfig(graph_entry.structure_);
        auto cpus = graph_entry.cpus_;
        graphs.push_back(omnistack::CreateGraph(graph_config));
        graph_names.push_back(graph_entry.name_);
        sub_graphs.push_back(&((*graphs.rbegin())->sub_graph_(0)));    // there is only one subgraph now
        sub_graph_cpus.push_back(cpus[0]);
    }

    /* 4. create engines */
    std::vector<omnistack::data_plane::EngineCreateInfo> engine_create_infos;
    for(int i = 0; i < sub_graphs.size(); i ++) {
        std::string name(graph_names[i]);
        name += "_subgraph_0";
        engine_create_infos.emplace_back(sub_graphs[i], sub_graph_cpus[i], name);
    }
    for(auto& info : engine_create_infos) {
        /* TODO: */
    }

    /* 5. start omnistack control plane */

    return 0;
}