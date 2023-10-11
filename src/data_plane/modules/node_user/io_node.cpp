#include <omnistack/module/module.hpp>
#include <mutex>
#include <omnistack/io/io_adapter.hpp>
#include <omnistack/node/node_common.h>
#include <omnistack/common/config.h>
#include <omnistack/common/logger.h>
#include <omnistack/common/time.hpp>

namespace omnistack::data_plane::io_node {
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "IoNode";
    constexpr int kMaxAdapters = 16;

    class IoNode : public Module<IoNode, kName> {
    public:
        IoNode() {}

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        constexpr bool has_timer_() override { return true; }

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;
    
    private:
        inline static std::once_flag driver_init_flag_;
        inline static io::BaseIoAdapter* adapters_[kMaxAdapters];
        io::IoRecvQueue* recv_queues_[kMaxAdapters];
        io::IoSendQueue* send_queues_[kMaxAdapters];
        inline static int num_adapters_;
        inline static int num_initialized_port_ = 0;
        inline static bool ports_initialized_ = false;
        inline static std::mutex initialized_port_mutex_;
        
        int id_;
        packet::PacketPool* packet_pool_;
        bool need_flush_[kMaxAdapters];
        int need_flush_stack_[kMaxAdapters];
        int need_flush_stack_top_;

        static void InitializeDrivers(IoNode* io_node) {
            auto init_func = [](io::BaseIoFunction* func) -> void {
                func->InitializeDriver();
            };
            io::ModuleFactory::instance_().Iterate(init_func);
            auto& adapter_configs = config::kStackConfig->GetNicConfigs();
            for (int i = 0; i < adapter_configs.size(); i ++) { /// TODO: iterate all needed nic
                auto& adapter_config = adapter_configs[i];
                auto adapter = io::ModuleFactory::instance_().Create(common::Crc32(adapter_config.driver_name_));
                if (adapter == nullptr) {
                    OMNI_LOG(common::kFatal) << "Cannot find driver " << adapter_config.driver_name_ << "\n";
                    exit(1);
                }
                auto mac_addr = adapter->InitializeAdapter(adapter_config.port_, config::kStackConfig->GetGraphEntries().size());
                memcpy(adapter_config.mac_raw_, mac_addr.raw, 6);
                adapters_[i] = adapter;
            }
            num_adapters_ = adapter_configs.size();
        }

// #define STATISTICS_RAW
#ifdef STATISTICS_RAW
        uint64_t packet_count_ = 0;
        uint64_t byte_count_ = 0;
        uint64_t last_time_ = 0;
#endif
    };

    void IoNode::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        std::call_once(driver_init_flag_, IoNode::InitializeDrivers, this);
        packet_pool_ = packet_pool;
        id_ = node_common::GetCurrentGraphId(std::string(name_prefix));

        for (int i = 0; i < num_adapters_; i ++) {
            auto queues = adapters_[i]->InitializeQueue(id_, packet_pool);
            recv_queues_[i] = queues.second;
            send_queues_[i] = queues.first;
            need_flush_[i] = false;
        }
        need_flush_stack_top_ = 0;

        {
            std::lock_guard<std::mutex> lock(initialized_port_mutex_);
            num_initialized_port_ ++;
            if (num_initialized_port_ == config::kStackConfig->GetGraphEntries().size()) { /// TODO: set number of queues
                for (int i = 0; i < num_adapters_; i ++) {
                    adapters_[i]->Start();
                }

                ports_initialized_ = true;
                OMNI_LOG(common::kInfo) << "All NIC initialized and started\n";
            }
        }

        while(ports_initialized_ == false) {
            usleep(1000);
        }
    }

    Packet* IoNode::MainLogic(Packet* packet) {
        // OMNI_LOG_TAG(kDebug, "IoNode") << "send packet to nic " << packet->nic_ << "\n";
        const auto nic = packet->nic_;
        send_queues_[nic]->SendPacket(packet);
        if (!need_flush_[nic]) {
            need_flush_[nic] = true;
            need_flush_stack_[need_flush_stack_top_ ++] = nic;
        }
        return nullptr;
    }

    Packet* IoNode::TimerLogic(uint64_t tick) {
        // OMNI_LOG_TAG(kDebug, "IoNode") << id_ << " IoNode timer logic\n";
        if (need_flush_stack_top_) [[unlikely]] {
            while (need_flush_stack_top_) {
                auto nic = need_flush_stack_[-- need_flush_stack_top_];
                send_queues_[nic]->FlushSendPacket();
                need_flush_[nic] = false;
            }
        }

#ifdef STATISTICS_RAW
        for (int i = 0; i < num_adapters_; i ++) {
            auto ret = recv_queues_[i]->RecvPackets();
            if(ret != nullptr) [[likely]] {
                while(ret != nullptr) {
                    packet_count_ ++;
                    if((packet_count_ & 0x7fffff) == 0) [[unlikely]] {
                        uint64_t now = omnistack::common::NowNs();
                        uint64_t pps = 1.0 * packet_count_ * 1000000000ULL / (now - last_time_);
                        OMNI_LOG_TAG(kInfo, "IoNode") << id_ << ": " << pps << " pps\n";
                        packet_count_ = 0;
                        last_time_ = now;
                    }
                    auto next = ret->next_packet_.Get();
                    ret->Release();
                    ret = next;
                }
            }
        }
        return nullptr;
#else
        for (int i = 0; i < num_adapters_; i ++) {
            auto ret = recv_queues_[i]->RecvPackets();
            if(ret != nullptr) [[likely]] {
                return ret;
            }
        }
        return nullptr;
#endif
    }
}