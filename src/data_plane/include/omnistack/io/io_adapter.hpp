//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_IO_IO_ADAPTER_H
#define OMNISTACK_IO_IO_ADAPTER_H

#include <vector>
#include <string>
#include <memory>
#include <map>
#include <iostream>
#include <functional>

#include <omnistack/packet/packet.hpp>
#include <omnistack/common/hash.hpp>

namespace omnistack {
    namespace io {
        class BaseIoAdapter {
        public:
            BaseIoAdapter() = default;
            virtual ~BaseIoAdapter() = default;

            virtual void InitializeAdapter(int port_id) = 0;
            virtual void InitializeQueue(int queue_id) = 0;

            virtual int AcqurieNumAdapters() = 0;
            virtual std::vector<int> AcquireUsablePortIds();
            virtual std::vector<std::string> AcquireUsablePortNames();

            virtual void SendPackets(int queue_id) = 0;

            virtual int RecvPackets(int queue_id) = 0;

            virtual void RedirectFlow(packet::Packet* packet);

            virtual constexpr uint32_t name_() { return common::ConstCrc32("BaseIoAdapter"); }
        };

        class ModuleFactory {
        public:
            typedef std::function<std::unique_ptr<BaseIoAdapter>()> CreateFunction;

            static ModuleFactory& instance_() {
                static ModuleFactory factory;
                return factory;
            }

            void Register(uint32_t name, const CreateFunction& func) {
                if(module_list_.find(name) != module_list_.end()) {
                    /* TODO: report error */
                    std::cerr << "module name conflict: " << name << "\n";
                    return;
                }
                if(func == nullptr) {
                    /* TODO: report error */
                    return;
                }
                if(!module_list_.insert(std::make_pair(name, func)).second) {
                    /* TODO: report error */
                    return;
                }
            }

            [[nodiscard]] std::unique_ptr<BaseIoAdapter> Create(uint32_t name) const {
                auto it = module_list_.find(name);
                if(it == module_list_.end()) {
                    /* TODO: report error */
                    return nullptr;
                }
                return it->second();
            }

        private:
            std::map<uint32_t, CreateFunction> module_list_;
        };

        template<typename T, const char name[]>
        class IoAdapter : public BaseIoAdapter {
            IoAdapter() { factory_entry_.DoNothing(); }
            virtual ~IoAdapter() { factory_entry_.DoNothing(); }

            static std::unique_ptr<BaseIoAdapter> CreateModuleObject() {
                return std::make_unique<T>();
            }

            constexpr uint32_t name_() override { return common::ConstCrc32(name); }

            struct FactoryEntry {
                FactoryEntry() {
                    ModuleFactory::instance_().Register(common::ConstCrc32(name), CreateModuleObject);
                }
                inline void DoNothing() const {}
            };

            static const FactoryEntry factory_entry_;
        };
    }
}

#endif //OMNISTACK_IO_IO_ADAPTER_H
