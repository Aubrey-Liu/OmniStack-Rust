#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/common/logger.h>
#include <omnistack/node.h>
#include <fstream>
#include <queue>
#include <omnistack/common/config.h>

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

        void AddPattern(std::string_view pattern);
    };

    void Ids::Initialize(std::string_view name_prefix, PacketPool* packet_pool) {
        auto conf = config::ConfigManager::GetModuleConfig("Ids");
        auto patterns = conf["patterns"];
        for(auto& pattern : patterns) {
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
    }

    void Ids::AddPattern(std::string_view pattern) {
        if(root_ == nullptr)
            root_ = new(memory::AllocateLocal(sizeof(ACANode))) ACANode();
        ACANode* p = root_;
        for(auto c : pattern) {
            if(p->next[c] == nullptr)
                p->next[c] = new ACANode();
            p = p->next[c];
        }
        p->count++;
    }

    packet::Packet* Ids::MainLogic(Packet* packet) {
        ACANode* p = root_;
        for (int i = packet->offset_; i < packet->length_; i ++) {
            p = p->next[packet->data_[i]];
            if (p->count > 0) {
                packet->Release();
                return nullptr;
            }
        }
        return packet;
    }
}