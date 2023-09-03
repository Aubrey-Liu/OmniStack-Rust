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

        constexpr bool allow_duplication_() override { return false; }

        constexpr ModuleType type_() override { return ModuleType::kReadOnly; }

    private:
        uint64_t packet_count_ = 0;
        uint64_t byte_count_ = 0;
        uint64_t last_time_ = 0;
    };

    Packet* SpeedTest::MainLogic(Packet* packet) {
        packet_count_ ++;
        byte_count_ += packet->GetLength();
        if(packet_count_ & 0x3fff) [[unlikely]] {
            uint64_t now = omnistack::common::NowNs();
            if(now - last_time_ > 1000000000ULL) {
                uint64_t pps = 1.0 * packet_count_ * 1000000000ULL / (now - last_time_);
                uint64_t bps = 1.0 * byte_count_ * 8 * 1000000000ULL / (now - last_time_);
                double Gbps = (double)bps / 1024.0 / 1024.0 / 1024.0;
                OMNI_LOG(kInfo) << "Speed test: " << pps << " pps, " << Gbps << " Gbps\n";
                packet_count_ = 0;
                byte_count_ = 0;
                last_time_ = now;
            }
        }
        return packet;
    }

}