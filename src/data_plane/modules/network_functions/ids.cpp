#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>
#include <omnistack/node.h>
#include <fstream>
#include <queue>
#include <omnistack/common/config.h>
#include <sys/time.h>

namespace omnistack::data_plane::ids {
    
    using namespace omnistack::common;
    using namespace omnistack::packet;

    inline constexpr char kName[] = "Ids";

    struct ACANode {
        ACANode* fail;
        ACANode* next[256];
        int count;

        ACANode() {
            fail = nullptr;
            count = 0;
            memset(next, 0, sizeof(next));
        }
    };

    class Ids : public Module<Ids, kName> {
    public:
        Ids() {}
        virtual ~Ids() {}

        Packet* MainLogic(Packet* packet) override;

        constexpr bool allow_duplication_() override { return true; }

        constexpr ModuleType type_() override { return ModuleType::kOccupy; }

        void Initialize(std::string_view name_prefix, PacketPool* packet_pool) override; 
    
    private:
        ACANode* root_;

        uint64_t packet_count = 0;
        uint64_t packet_size_sum = 0;
        uint64_t last_print_us = 0;

        void AddPattern(std::string_view pattern);
    };

    void Ids::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        auto conf = config::ConfigManager::GetModuleConfig("Ids");
        auto patterns = conf["patterns"];

        root_ = new(memory::AllocateLocal(sizeof(ACANode))) ACANode();
        for(auto& pattern : patterns) {
            OMNI_LOG(common::kInfo) << "Add pattern " << pattern.asString() << "\n";
            AddPattern(pattern.asString());
        }

        /** Build Fail Tree of ACA **/
        {
            std::queue<ACANode*> q;
            for (int i = 0; i < 256; i ++) {
                if (root_->next[i] != nullptr) {
                    root_->next[i]->fail = root_;
                    q.push(root_->next[i]);
                } else {
                    root_->next[i] = root_;
                }
            }

            while (!q.empty()) {
                ACANode* p = q.front();
                q.pop();
                for (int i = 0; i < 256; i ++) {
                    if (p->next[i] != nullptr) {
                        p->next[i]->fail = p->fail->next[i];
                        q.push(p->next[i]);
                    } else
                        p->next[i] = p->fail->next[i];
                }
            }
        }

        packet_count = 0;
        packet_size_sum = 0;
        last_print_us = 0;
    }

    void Ids::AddPattern(std::string_view pattern) {
        ACANode* p = root_;
        for(auto c : pattern) {
            auto idx = (uint8_t)c;
            if(p->next[idx] == nullptr)
                p->next[idx] = new(memory::AllocateLocal(sizeof(ACANode))) ACANode();
            p = p->next[idx];
        }
        p->count++;
    }

    packet::Packet* Ids::MainLogic(Packet* packet) {
        packet_count ++;
        packet_size_sum += packet->GetLength();
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        auto current_us = tv.tv_sec * 1000000 + tv.tv_usec;

        if (current_us - last_print_us > 1000000) {
            OMNI_LOG(common::kInfo) << "OmniStackIDS: " << packet_count << " packets, " << packet_size_sum << " bytes, " << packet_size_sum / packet_count << " bytes/packet\n";
            OMNI_LOG(common::kInfo) << "OmniStackIDS: " << packet_size_sum * 8.0 / (current_us - last_print_us) << " Mbps\n";
            // click_chatter("OmniStackIDS: %lu packets, %lu bytes, %lu bytes/packet", packet_count, packet_size_sum, packet_size_sum / packet_count);
            // click_chatter("OmniStackIDS: %.3f Mbps", packet_size_sum * 8.0 / (current_us - last_print_us));
            
            packet_count = 0;
            packet_size_sum = 0;
            last_print_us = current_us;
        }

        ACANode* p = root_;
        auto ptr = packet->GetPayload();
        auto length = packet->GetLength();
        for (int i = 0; i < length; i ++) {
            p = p->next[(uint8_t)ptr[i]];
            if (p->count > 0) {
                packet->Release();
                return nullptr;
            }
        }
        return packet;
    }
}