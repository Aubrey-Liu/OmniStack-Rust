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
    config::StackConfig* kStackConfig;

    static inline void InitializeMemory() {
#if defined (OMNIMEM_BACKEND_DPDK)
        memory::StartControlPlane(true);
#else
        memory::StartControlPlane();
#endif
        memory::InitializeSubsystem();
        memory::InitializeSubsystemThread();
        memory::BindedCPU(0); // Use this to avoid warnings, This may cause performance issues ?
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

    static std::vector<pthread_t> engine_threads;
    static std::vector<volatile bool*> stop_flag;

    static void* EngineThreadEntry(void* arg) {
        memory::InitializeSubsystemThread();
        auto create_info = reinterpret_cast<data_plane::EngineCreateInfo*>(arg);
        auto engine = data_plane::Engine::Create(*create_info);
        stop_flag[create_info->engine_id] = engine->GetStopFlag();
        engine->Run();
        OMNI_LOG(kInfo) << "Engine " << create_info->engine_id << " stopped\n";
        data_plane::Engine::Destroy(engine);
        return nullptr;
    }

    static void SigintHandler(int sig) {
        OMNI_LOG(kInfo) << "SIGINT received, stopping all threads\n";
        for(int i = 0; i < stop_flag.size(); i ++) {
            if(stop_flag[i] != nullptr) {
                OMNI_LOG(kInfo) << "Engine " << i << " stop flag set\n";
                *stop_flag[i] = true;
            }
        }
    }

}

int main(int argc, char **argv) {
    /* 0. Initialize Logger */
    Logger::Initialize(std::cerr, "log/log");

    /* 1. load configurations */
    omnistack::config::ConfigManager::LoadFromDirectory("../../config");
    omnistack::config::ConfigManager::LoadFromDirectory("../../graph_config");
    std::string config_name = argc > 1 ? argv[1] : "config";
    auto stack_config = omnistack::config::ConfigManager::GetStackConfig(config_name);
    omnistack::kStackConfig = &stack_config;

    OMNI_LOG(kInfo) << "Stack config loaded\n";

    /* 2. init libraries */
    omnistack::InitializeMemory();
    omnistack::InitializeToken();
    OMNI_LOG(kInfo) << "Memory and token initialized\n";
    omnistack::InitializeChannel();
    OMNI_LOG(kInfo) << "Channel initialized\n";
    omnistack::InitializeNode(stack_config.GetGraphEntries().size());
    OMNI_LOG(kInfo) << "Node initialized\n";

    OMNI_LOG(kInfo) << "Libraries initialized\n";

    /* 3. load dynamic link libraries */
    auto dynamic_links = stack_config.GetDynamicLinkEntries();
    for(auto& dynamic_link : dynamic_links) {
        auto& directory = dynamic_link.directory;
        auto& library_names = dynamic_link.library_names;
        DynamicLink::Load(directory, library_names);
    }

    OMNI_LOG(kInfo) << "Dynamic link libraries loaded\n";

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
        OMNI_LOG(kDebug) << "Engine prefix = " << name << "\n";
        engine_create_infos.emplace_back(i, sub_graphs[i], sub_graph_cpus[i], name);
    }
    omnistack::engine_threads.resize(engine_create_infos.size());
    omnistack::stop_flag.resize(engine_create_infos.size(), nullptr);
    for(auto& info : engine_create_infos) {
        auto ret = CreateThread(&omnistack::engine_threads[info.engine_id], omnistack::EngineThreadEntry, &info);
    }

    OMNI_LOG(kInfo) << "Engines created\n";

    /* 6. register sigint handler */
    {
        auto ret = signal(SIGINT, omnistack::SigintHandler);
        if (ret == SIG_ERR) {
            OMNI_LOG(kError) << "Failed to register sigint handler\n";
            exit(1);
        } else
            OMNI_LOG(kInfo) << "Sigint handler registered\n";
    }

    /* 7. start omnistack control plane */

    /* 8. join threads */
    for(int i = 0; i < engine_create_infos.size(); i ++)
        JoinThread(omnistack::engine_threads[i], nullptr);

    return 0;
}