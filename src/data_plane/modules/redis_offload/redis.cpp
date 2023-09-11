#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <arpa/inet.h>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <omnistack/common/logger.h>

namespace omnistack::data_plane::redis_offload {

    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "RedisOffload";

    enum class FlowStatus {
        kIdle,
        kMultiLine,
        kSingleLine,
        kError,
        kInteger,
        kArray
    };

    struct FlowInfo {
        FlowStatus status = FlowStatus::kIdle;
        int restline = 0;
        Packet* packet = nullptr;
    };

    class RedisOffload : public Module<RedisOffload, kName> {
    public:
        RedisOffload() {}

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override;
    
    private:
        std::unordered_map<std::string, Packet*> redis_;
        PacketPool* packet_pool_;
    };

    void RedisOffload::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        packet_pool_ = packet_pool;

        redis_.clear();
    }

    Packet* RedisOffload::MainLogic(Packet* packet) {
        auto tcph = packet->GetL4Header<TcpHeader>();
        // OMNI_LOG_TAG(common::kDebug, "Redis") << "RECV REQUEST dport = " << ntohs(tcph->dport) << "\n";
        if (ntohs(tcph->dport) != 6379)
            return packet;
        
        auto payload = packet->GetPayload();

        {
            bool supported_cmd = true;
            constexpr auto cmd_len = strlen("*3\r\n");
            auto old_c = payload[cmd_len];
            payload[cmd_len] = '\0';
            if (strcmp(payload, "*3\r\n") != 0)
                supported_cmd = false;
            payload[cmd_len] = old_c;
            payload += cmd_len;
            if (!supported_cmd) {
                OMNI_LOG(common::kFatal) << "Should not appear in test\n";
                exit(1);
                return packet;
            }
        }

        bool is_set = false;
        bool is_get = false;
        {
            constexpr auto cmd_len = strlen("$3\r\nSET\r\n");
            auto old_c = payload[cmd_len];
            payload[cmd_len] = '\0';
            if (strcmp(payload, "$3\r\nSET\r\n") == 0)
                is_set = true;
            else if (strcmp(payload, "$3\r\nGET\r\n") == 0)
                is_get = true;
            payload[cmd_len] = old_c;
            payload += cmd_len;
            if (!is_get && !is_set) {
                OMNI_LOG(common::kFatal) << "Should not appear in test\n";
                exit(1);
                return packet;
            }
        }

        std::string key;
        {
            auto key_len = 0;
            payload ++; // Skip the '$'
            while (*payload != '\r') {
                key_len = key_len * 10 + (*payload - '0');
                payload ++;
            }
            payload += 2; // Skip the '\r\n'
            payload[key_len] = '\0';
            key = std::string(payload, key_len);
            payload += key_len + 2; // Skip the '\r\n'
        }

        if (is_set) {
            packet->offset_ += payload - packet->GetPayload();
            if (redis_.find(key) != redis_.end()) {
                redis_[key]->Release();
            }
            redis_[key] = packet;

            // Send OK Back
            auto ok_packet = packet_pool_->Allocate();
            auto ok_payload = ok_packet->GetPayload();
            memcpy(ok_payload, "+OK\r\n", 5);
            ok_packet->SetLength(5);
            ok_packet->node_ = packet->node_;
            return ok_packet;
        } else {
            if (redis_.find(key) == redis_.end()) {
                // Unhandle
                auto err_packet = packet_pool_->Allocate();
                auto err_payload = err_packet->GetPayload();
                memcpy(err_payload, "-ERR\r\n", 6);
                err_packet->SetLength(6);
                err_packet->node_ = packet->node_;
                return err_packet;
            } else {
                auto ref = packet_pool_->Reference(redis_[key]);
                return ref;
            }
        }

        packet->Release();
        return nullptr;
    }
}