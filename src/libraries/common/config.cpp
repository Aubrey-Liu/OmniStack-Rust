#include <omnistack/common/config.h>
#include <omnistack/common/logger.h>
#include <filesystem>
#include <fstream>

namespace omnistack::config
{
    void ConfigManager::LoadFromDirectory(const std::string& path) {
        std::filesystem::path p(path);
        if (!std::filesystem::exists(p) || !std::filesystem::is_directory(p)) {
            std::cerr << "Config directory " << path << " does not exist" << std::endl;
            exit(1);
        }
        for (const auto& entry : std::filesystem::directory_iterator(p)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                LoadFromFile(entry.path().string());
            }
        }
    }

    std::map<std::string, GraphConfig*> ConfigManager::graph_configs_;
    std::map<std::string, StackConfig*> ConfigManager::stack_configs_;

    void ConfigManager::LoadFromFile(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            std::cerr << "Config file " << path << " does not exist" << std::endl;
            exit(1);
        }
        Json::Value root;
        Json::CharReaderBuilder builder;
        JSONCPP_STRING errs;
        if (!Json::parseFromStream(builder, ifs, &root, &errs)) {
            std::cerr << "Failed to parse config file " << path << std::endl;
            exit(1);
        }
        if (root["type"].isNull()) {
            OMNI_LOG(common::kFatal) << "Config file " << path << " does not have a type\n";
            exit(1);
        }
        if (!root["type"].isString()) {
            OMNI_LOG(common::kFatal) << "Config file " << path << " has a non-string type\n";
            exit(1);
        }
        if (root["name"].isNull()) {
            OMNI_LOG(common::kFatal) << "Config file " << path << " does not have a name\n";
            exit(1);
        }
        if (!root["name"].isString()) {
            OMNI_LOG(common::kFatal) << "Config file " << path << " has a non-string name\n";
            exit(1);
        }
        std::string type = root["type"].asString();
        std::string name = root["name"].asString();
        if (type == "Graph") {
            if (graph_configs_.count(name)) {
                OMNI_LOG(common::kFatal) << "Graph config " << name << " already exists\n";
                exit(1);
            }
            graph_configs_[name] = new GraphConfig(root);
            OMNI_LOG(common::kInfo) << "Graph config " << name << " loaded\n";
        } else if (type == "Stack") {
            if (stack_configs_.count(name)) {
                OMNI_LOG(common::kFatal) << "Stack config " << name << " already exists\n";
                exit(1);
            }
            stack_configs_[name] = new StackConfig(root);
            OMNI_LOG(common::kInfo) << "Stack config " << name << " loaded\n";
        } else {
            OMNI_LOG(common::kFatal) << "Config file " << path << " has an unknown type " << type << "\n";
            exit(1);
        }
    }

    const GraphConfig& ConfigManager::GetGraphConfig(const std::string& name) {
        if (graph_configs_.find(name) == graph_configs_.end()) {
            OMNI_LOG(common::kFatal) << "Graph config " << name << " not found\n";
            exit(1);
        }
        return *graph_configs_[name];
    }

    const StackConfig& ConfigManager::GetStackConfig(const std::string& name) {
        if (stack_configs_.find(name) == stack_configs_.end()) {
            OMNI_LOG(common::kFatal) << "Stack config " << name << " not found\n";
            exit(1);
        }
        return *stack_configs_[name];
    }
} // namespace omnistack::config
