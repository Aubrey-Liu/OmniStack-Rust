//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_ENGINE_ENGINE_HPP
#define OMNISTACK_ENGINE_ENGINE_HPP

#include <omnistack/graph/graph.hpp>
#include <omnistack/module/module.hpp>
#include <omnistack/channel/channel.h>
#include <unordered_map>

namespace omnistack::data_plane {
    /* Engine receives a SubGraph and runs it */

    constexpr uint32_t kMaxQueueSize = 65536;

    /* TODO: add functionality to link to any module in graph */

    struct EngineCreateInfo {
        uint32_t engine_id;
        SubGraph* sub_graph;
        uint32_t logic_core;
        std::string name_prefix;
    };

    class Engine {
    public:
        static Engine* Create(EngineCreateInfo& info);

        void Run();

        static void Destroy(Engine* engine);

        static void RaiseEvent(Event* event) { current_engine_->HandleEvent(event); }

        volatile bool* GetStopFlag() { return &stop_; }

    private:
        Engine() = default;
        ~Engine();
        Engine(const Engine&) = delete;
        Engine(Engine&&) = delete;

        void Init(SubGraph& sub_graph, uint32_t core, std::string_view name_prefix);

        void HandleEvent(Event* event);

        void ForwardPacket(Packet* &packet, uint32_t node_idx);

        bool CompareLinks(uint32_t x, uint32_t y);

        void SortLinks(std::vector<uint32_t>& links);

        static thread_local Engine* current_engine_;
        static thread_local volatile bool stop_;

        typedef std::pair<uint32_t, Packet*> QueueItem;
        QueueItem packet_queue_[kMaxQueueSize];
        uint32_t packet_queue_count_ = 0;

        /* event info */
        std::unordered_map<Event::EventType, std::vector<uint32_t>> event_entries_;

        /* timer info */
        std::vector<uint32_t> timer_list_;

        /* graph info */
        uint32_t module_num_;
        uint32_t assigned_module_idx_;
        std::vector<BaseModule*> modules_;
        std::vector<uint32_t> module_name_crc32_;
        std::vector<std::vector<uint32_t>> upstream_links_;
        std::vector<std::vector<uint32_t>> downstream_links_;
        std::vector<uint32_t> local_to_global;
        std::vector<uint32_t> timer_modules_;
        // std::vector<std::pair<Channel, uint32_t>> receive_channels_;
        // std::vector<Channel> send_channels_;

        /* runtime structures */
        PacketPool* packet_pool_;

        /* helper structures */
        std::vector<uint32_t> next_hop_filter_default_;
        std::vector<bool> module_read_only_;
    };
}

#endif //OMNISTACK_ENGINE_ENGINE_HPP
