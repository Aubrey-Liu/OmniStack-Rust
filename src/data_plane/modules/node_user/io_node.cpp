#include <omnistack/module/module.hpp>
#include <mutex>
#include <omnistack/io/io_adapter.hpp>
#include <omnistack/node/node_common.h>

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

        Packet* MainLogic(Packet* packet) override;

        Packet* TimerLogic(uint64_t tick) override;
    
    private:
        static std::once_flag driver_init_flag_;
        static io::BaseIoAdapter* adapters_[kMaxAdapters];
        static int num_adapters_;
        static int num_initialized_port_;
        static std::mutex initialized_port_mutex_;
        
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
            for (int i = 0; i < 10; i ++) { /// TODO: iterate all needed nic
                auto adapter = io::ModuleFactory::instance_().Create(common::ConstCrc32("DpdkAdapter"));
                adapter->InitializeAdapter(i);
                adapters_[i] = adapter.release();
            }
            num_adapters_ = 10;
        }
    };

    

    void IoNode::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        std::call_once(driver_init_flag_, IoNode::InitializeDrivers, this);
        packet_pool_ = packet_pool;
        id_ = node_common::GetCurrentGraphId(std::string(name_prefix));

        for (int i = 0; i < num_adapters_; i ++) {
            adapters_[i]->InitializeQueue(id_, packet_pool);
            need_flush_[i] = false;
        }
        need_flush_stack_top_ = 0;

        {
            std::lock_guard<std::mutex> lock(initialized_port_mutex_);
            num_initialized_port_ ++;

            if (num_initialized_port_ == 3) { /// TODO: set number of queues
                for (int i = 0; i < num_adapters_; i ++) {
                    adapters_[i]->Start();
                }
            }
        }
    }

    Packet* IoNode::MainLogic(Packet* packet) {
        const auto nic = packet->nic_;
        adapters_[nic]->SendPacket(id_, packet);
        if (!need_flush_[nic]) {
            need_flush_[nic] = true;
            need_flush_stack_[need_flush_stack_top_ ++] = nic;
        }
        return nullptr;
    }

    Packet* IoNode::TimerLogic(uint64_t tick) {
        if (need_flush_stack_top_) [[unlikely]] {
            while (need_flush_stack_top_) {
                auto nic = need_flush_stack_[-- need_flush_stack_top_];
                adapters_[nic]->FlushSendPacket(id_);
                need_flush_[nic] = false;
            }
        }

        for (int i = 0; i < num_adapters_; i ++) {
            auto pkt = adapters_[i]->RecvPackets(id_);
            if (pkt != nullptr) [[likely]] {
                return pkt;
            }
        }
        
        return nullptr;
    }
}