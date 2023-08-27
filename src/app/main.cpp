#include <omnistack/common/config.h>
#include <omnistack/common/constant.hpp>
#include <omnistack/common/cpu.hpp>
#include <omnistack/common/thread.hpp>
#include <omnistack/common/dynamic_link.hpp>
#include <omnistack/memory/memory.h>
#include <omnistack/common/logger.h>
#include <omnistack/token/token.h>
#include <omnistack/node.h>
#include <omnistack/engine/engine.hpp>
#include <csignal>

using namespace omnistack::common;

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

    static inline void InitializeNode(int num_graph) {
        node::StartControlPlane(num_graph);
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

    static pthread_t engine_threads[common::kMaxThread];
    static volatile bool* stop_flag[common::kMaxThread];

    static void* EngineThreadEntry(void* arg) {
        memory::InitializeSubsystemThread();
        auto create_info = reinterpret_cast<data_plane::EngineCreateInfo*>(arg);
        auto engine = data_plane::Engine::Create(*create_info);
        stop_flag[create_info->engine_id] = engine->GetStopFlag();
        engine->Run();
        data_plane::Engine::Destroy(engine);
        return nullptr;
    }

    static void SigintHandler(int sig) {
        for(int i = 0; i < common::kMaxThread; i ++) {
            if(stop_flag[i] != nullptr)
                *stop_flag[i] = true;
        }
    } 

}

int main(int argc, char **argv) {
    /* 0. Initialize Logger */
    omnistack::common::Logger::Initialize(std::cerr, "log/log");

    /* 1. load configurations */
    omnistack::config::ConfigManager::LoadFromDirectory("../../config");
    omnistack::config::ConfigManager::LoadFromDirectory("../../graph_config");
    std::string config_name = argc > 1 ? argv[1] : "config";
    auto stack_config = omnistack::config::ConfigManager::GetStackConfig(config_name);

    OMNI_LOG(omnistack::common::kInfo) << "Stack config loaded\n";

    /* 2. init libraries */
    omnistack::InitializeMemory();
    omnistack::InitializeToken();
    omnistack::InitializeChannel();
    omnistack::InitializeNode(stack_config.GetGraphEntries().size());

    OMNI_LOG(omnistack::common::kInfo) << "Libraries initialized\n";

    /* 3. load dynamic link libraries */
    auto dynamic_links = stack_config.GetDynamicLinkEntries();
    for(auto& dynamic_link : dynamic_links) {
        auto& directory = dynamic_link.directory;
        auto& library_names = dynamic_link.library_names;
        omnistack::common::DynamicLink::Load(directory, library_names);
    }

    OMNI_LOG(omnistack::common::kInfo) << "Dynamic link libraries loaded\n";

    /* 4. create graphs, directly using graph config at present, each graph has only one subgraph */
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

    OMNI_LOG(kInfo) << "Graphs created\n";

    /* 5. create engines */
    std::vector<omnistack::data_plane::EngineCreateInfo> engine_create_infos;
    for(int i = 0; i < sub_graphs.size(); i ++) {
        std::string name(graph_names[i]);
        name += "_subgraph_0";
        engine_create_infos.emplace_back(i, sub_graphs[i], sub_graph_cpus[i], name);
    }
    for(auto& info : engine_create_infos) {
        auto ret = omnistack::common::CreateThread(&omnistack::engine_threads[info.engine_id], omnistack::EngineThreadEntry, &info);
    }

    OMNI_LOG(kInfo) << "Engines created\n";

    /* 6. register sigint handler */
    signal(SIGINT, omnistack::SigintHandler);

    /* 7. start omnistack control plane */

    /* 8. join threads */
    for(int i = 0; i < engine_create_infos.size(); i ++)
        omnistack::common::JoinThread(omnistack::engine_threads[i], nullptr);

    return 0;
}