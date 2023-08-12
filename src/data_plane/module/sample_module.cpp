//
// Created by liuhao on 23-8-6.
//

#include <omnistack/module/module.hpp>
#include <cstdarg>

namespace omnistack::data_plane::sample_module {

    inline constexpr char kName[] = "SampleModule";

    inline void Message(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }

    class SampleModule : public Module<SampleModule, kName> {
    public:
        SampleModule() {}

        static bool DefaultFilter(Packet*) {
            Message("DefaultFilter\n");
            return true;
        }

        Filter GetFilter(std::string_view upstream_module, uint32_t global_id) override {
            Message("GetFilter\n");
            return DefaultFilter;
        }

        Packet* MainLogic(Packet* packet) override {
            Message("MainLogic\n");
            return packet;
        }
        
        Packet* TimerLogic(uint64_t tick) override {
            Message("TimerLogic\n");
            return nullptr;
        }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override {
            Message("Init\n");
        }

        void Destroy() override {
            Message("Destroy\n");
        }

        bool allow_duplication_() override {
            Message("allow_duplication_\n");
            return true;
        }

        ModuleType type_() override {
            Message("type_\n");
            return ModuleType::kReadOnly;
        }
    };
}