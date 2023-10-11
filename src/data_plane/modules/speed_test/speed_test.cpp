//
// Created by liuhao on 23-8-8.
//

#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>
#include <omnistack/common/time.hpp>

namespace omnistack::data_plane::speed_test {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "SpeedTest";

    class SpeedTest : public Module<SpeedTest, kName> {
    public:
        SpeedTest() {}

        Packet* MainLogic(Packet* packet) override;

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kReadOnly; }

    private:
        std::string name_prefix_;
        uint64_t packet_count_ = 0;
        uint64_t byte_count_ = 0;
        uint64_t last_time_ = 0;
    };

    Packet* SpeedTest::MainLogic(Packet* packet) {
        // OMNI_LOG_TAG(kInfo, "SpeedTest") << name_prefix_ << ": " << packet->GetLength() << " bytes\n";
        packet_count_ ++;
        byte_count_ += packet->GetLength();
        uint64_t now = omnistack::common::NowNs();
        if(now - last_time_ > 1000000000ULL) {
            uint64_t pps = 1.0 * packet_count_ * 1000000000ULL / (now - last_time_);
            uint64_t bps = 1.0 * byte_count_ * 8 * 1000000000ULL / (now - last_time_);
            double Gbps = (double)bps / 1024.0 / 1024.0 / 1024.0;
            OMNI_LOG_TAG(kInfo, "SpeedTest") << name_prefix_ << ": " << pps << " pps, " << Gbps << " Gbps\n";
            packet_count_ = 0;
            byte_count_ = 0;
            last_time_ = now;
        }
        return packet;
    }

    void SpeedTest::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        name_prefix_ = name_prefix;
        last_time_ = omnistack::common::NowNs();
    }

}