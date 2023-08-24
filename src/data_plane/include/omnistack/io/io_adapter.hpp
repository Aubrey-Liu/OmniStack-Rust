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
        enum class NICPortType {
            kPortIndex = 0,
            kPortName
        };

        class NICInfo {
        public:
            std::string driver_name_;
            int port_id_;
            std::string port_name_;
            NICPortType port_type_;
        };

        class BaseIoFunction {
        public:
            BaseIoFunction() = default;
            virtual ~BaseIoFunction() = default;

            virtual void InitializeDriver() = 0;
            
            virtual int AcqurieNumAdapters() = 0;
            virtual std::vector<int> AcquireUsablePortIds();
            virtual std::vector<std::string> AcquireUsablePortNames();

            virtual constexpr uint32_t name_() { return common::ConstCrc32("BaseIoFunction"); }
        };

        class BaseIoAdapter : public BaseIoFunction {
        public:
            BaseIoAdapter() = default;
            virtual ~BaseIoAdapter() = default;

            virtual void InitializeAdapter(int port_id) = 0;

            virtual void InitializeQueue(int queue_id, packet::PacketPool* packet_pool) = 0;

            virtual void SendPacket(int queue_id, packet::Packet* packet) = 0;
            /** Periodcally called **/
            virtual void FlushSendPacket(int queue_id) = 0;

            virtual void Start() = 0;

            virtual packet::Packet* RecvPackets(int queue_id) = 0;

            virtual void RedirectFlow(packet::Packet* packet);

            constexpr uint32_t name_() const { return common::ConstCrc32("BaseIoAdapter"); }
        };

        class ModuleFactory {
        public:
            typedef std::function<std::unique_ptr<BaseIoAdapter>()> CreateFunction;
            typedef std::function<void(BaseIoFunction* function)> IterateFunction;

            static ModuleFactory& instance_() {
                static ModuleFactory factory;
                return factory;
            }

            void Register(uint32_t name, const CreateFunction& func) {
                if(driver_list_.find(name) != driver_list_.end()) {
                    /* TODO: report error */
                    std::cerr << "module name conflict: " << name << "\n";
                    return;
                }
                if(func == nullptr) {
                    /* TODO: report error */
                    return;
                }
                if(!driver_list_.insert(std::make_pair(name, func)).second) {
                    /* TODO: report error */
                    return;
                }
                driver_instance_list_.emplace_back(Create(name).get());
            }

            [[nodiscard]] std::unique_ptr<BaseIoAdapter> Create(uint32_t name) const {
                auto it = driver_list_.find(name);
                if(it == driver_list_.end()) {
                    /* TODO: report error */
                    return nullptr;
                }
                return it->second();
            }

            void Iterate(IterateFunction func) {
                for(auto& instance : driver_instance_list_) {
                    func(reinterpret_cast<BaseIoFunction*>(instance));
                }
            }

        private:

            std::map<uint32_t, CreateFunction> driver_list_;
            std::vector<BaseIoAdapter*> driver_instance_list_;
        };

        template<typename T, const char name[]>
        class IoAdapter : public BaseIoAdapter {
        public:
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
