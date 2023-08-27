//
// Created by liuhao on 23-8-27.
//

#ifndef OMNISTACK_COMMON_DYNAMIC_LINK_HPP
#define OMNISTACK_COMMON_DYNAMIC_LINK_HPP

#include <dlfcn.h>
#include <omnistack/common/hash.hpp>
#include <omnistack/common/logger.h>
#include <string>
#include <vector>
#include <map>

namespace omnistack::common {

    class DynamicLink {
    public:
        static void* Load(const std::string& path, const std::string& name) {
            std::string full_path(path);
            if(path.back() != '/') full_path += '/';
            full_path += name;
            auto handle = LoadDynamicLibrary(full_path);
            if(handle != nullptr) handles[Crc32(name)] = handle;
            return handle;
        }

        static void Load(const std::string& path, const std::vector<std::string>& names) {
            for (auto& name : names) {
                auto handle = Load(path, name);
                if(handle != nullptr) handles[Crc32(name)] = handle;
            }
        }

    private:
        static void* LoadDynamicLibrary(const std::string& path) {
            auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if(handle == nullptr) {
                OMNI_LOG(kError) << "Failed to load dynamic library " << path << "\n";
                OMNI_LOG(kError) << dlerror() << "\n";
            }
            else OMNI_LOG(kInfo) << "Loaded dynamic library " << path << "\n";
            return handle;
        }

        inline static std::map<uint32_t, void*> handles;
    };

}

#endif // OMNISTACK_COMMON_DYNAMIC_LINK_HPP