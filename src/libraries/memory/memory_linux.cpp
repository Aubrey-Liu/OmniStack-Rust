//
// Created by Jeremy Guo on 2023/6/17.
//

#include <omnistack/memory/memory.h>

namespace omnistack {
    namespace mem {
#if !defined(OMNIMEM_BACKEND_DPDK)
        void* AllocateLocal(size_t size) {
            auto ret = malloc(size + kMetaHeadroomSize);
            auto meta = (RegionMeta*)ret;
            ret = reinterpret_cast<char*>(ret) + kMetaHeadroomSize;

            meta->type = RegionType::kLocal;
            meta->iova = 0;
            meta->size = size;
            meta->thread_id = thread_id;
            meta->offset = 0;

            return ret;
        }
#endif
    }
}