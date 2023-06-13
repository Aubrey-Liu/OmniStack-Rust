//
// Created by liuhao on 23-6-10.
//

#include <omnistack/engine/engine.hpp>

namespace omnistack::data_plane {
    bool Engine::CompareLinks(uint32_t x, uint32_t y) {
        int t1 = x < module_num_;
        int t2 = y < module_num_;
        if(t1 != t2) return t1 < t2;
        if(modules_[x].type_ == ModuleType::kReadOnly) return true;
        return modules_[y].type_ != ModuleType::kReadOnly;
    }

    void Engine::SortLinks(std::vector<uint32_t> &links) {
        for(int i = 0; i < links.size(); i ++)
            for(int j = 0; j < links.size() - 1; j ++)
                if(!CompareLinks(links[j], links[j + 1]))
                    std::swap(links[j], links[j + 1]);
                else break;
    }

    void Engine::Init(omnistack::data_plane::SubGraph &sub_graph, uint32_t core) {}

    void Engine::Destroy() {}

    void Engine::Run() {}
}
