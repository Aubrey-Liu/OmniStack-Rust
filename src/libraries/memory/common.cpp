//
// Created by Jeremy Guo on 2023/6/17.
//

#include <omnistack/memory/memory.h>
#include <thread>
#include <condition_variable>

namespace omnistack {
    namespace mem {
        uint64_t process_id = ~0;
        thread_local uint64_t thread_id = ~0;

        void Free(void* ptr) {
            auto meta = (RegionMeta*)(
                    reinterpret_cast<char*>(ptr) - kMetaHeadroomSize
                    );
            switch (meta->type) {
                case RegionType::kLocal:
                    FreeLocal(ptr);
                    break;
                case RegionType::kShared:
                    FreeShared(ptr);
                    break;
                case RegionType::kNamedShared:
                    FreeNamedShared(ptr);
                    break;
                case RegionType::kMempoolChunk:
                    MemoryPool::PutBack(ptr);
                    break;
            }
        }

        void MemoryPool::PutBack(void *ptr) {
            auto meta = (RegionMeta*)( reinterpret_cast<char*>(ptr) - kMetaHeadroomSize );
            if (meta->type == RegionType::kMempoolChunk) {
                if (meta->mempool != nullptr)
                    meta->mempool->Put(ptr);
                else throw std::runtime_error("Chunk's mempool is nullptr");
            } else throw std::runtime_error("Ptr is not a chunk");
        }

        static std::thread* control_plane_thread = nullptr;
        static std::condition_variable cond_control_plane_started;
        static std::mutex control_plane_state_lock;
        static bool control_plane_started = false;
        static volatile bool stop_control_plane = false;

        void ControlPlane() {
            {
                std::unique_lock<std::mutex> _(control_plane_state_lock);
                control_plane_started = true;
                cond_control_plane_started.notify_all();
            }

            while (!stop_control_plane) {

            }
        }

        void StartControlPlane() {
            std::unique_lock<std::mutex> _(control_plane_state_lock);
            control_plane_thread = new std::thread(ControlPlane);
            cond_control_plane_started.wait(_, [&](){
                return !control_plane_started;
            });
        }

        void StopControlPlane() {
            stop_control_plane = true;
            if (control_plane_thread != nullptr) {
                control_plane_thread->join();
                delete control_plane_thread;
                control_plane_thread = nullptr;
            } else throw std::runtime_error("Control Plane is nullptr");
            control_plane_started = false;
            stop_control_plane = false;
        }
    }
}