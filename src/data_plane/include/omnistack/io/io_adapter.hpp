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
#include <omnistack/common/logger.h>

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

        class IoSendQueue {
        public:
            IoSendQueue() = default;
            virtual ~IoSendQueue() = default;

            virtual void SendPacket(packet::Packet* packet) = 0;
            /** Periodcally called **/
            virtual void FlushSendPacket() = 0;
        };

        class IoRecvQueue {
        public:
            IoRecvQueue() = default;
            virtual ~IoRecvQueue() = default;

            virtual packet::Packet* RecvPacket() = 0;

            virtual packet::Packet* RecvPackets() { return RecvPacket(); }

            virtual void RedirectFlow(packet::Packet* packet);
        };

        class BaseIoAdapter : public BaseIoFunction {
        public:
            BaseIoAdapter() = default;
            virtual ~BaseIoAdapter() = default;

            virtual common::MacAddr InitializeAdapter(int port_id, int num_queues) = 0;

            virtual std::pair<IoSendQueue*, IoRecvQueue*> InitializeQueue(int queue_id, packet::PacketPool* packet_pool) = 0;

            virtual void Start() = 0;

            virtual constexpr uint32_t name_() const { return common::ConstCrc32("BaseIoAdapter"); }
        };

        class ModuleFactory {
        public:
            typedef std::function<BaseIoAdapter*()> CreateFunction;
            typedef std::function<void(BaseIoFunction* function)> IterateFunction;

            static ModuleFactory& instance_() {
                static ModuleFactory factory;
                return factory;
            }

            void Register(uint32_t name, const CreateFunction& func) {
                if(driver_list_.find(name) != driver_list_.end()) {
                    OMNI_LOG(common::kFatal) << "Module name conflict: " << name << "\n";
                    return;
                }
                if(func == nullptr) {
                    OMNI_LOG(common::kFatal) << "Module create function is null: " << name << "\n";
                    return;
                }
                if(!driver_list_.insert(std::make_pair(name, func)).second) {
                    OMNI_LOG(common::kFatal) << "Module register failed: " << name << "\n";
                    return;
                }
                driver_instance_list_.emplace_back(Create(name));
            }

            [[nodiscard]] BaseIoAdapter* Create(uint32_t name) const {
                auto it = driver_list_.find(name);
                if(it == driver_list_.end()) {
                    OMNI_LOG(common::kFatal) << "Module not found: " << name << "\n";
                    return nullptr;
                }
                return it->second();
            }

            void Iterate(IterateFunction func) {
                for(auto& instance : driver_instance_list_) {
                    func(static_cast<BaseIoFunction*>(instance));
                }
            }

        private:

            std::map<uint32_t, CreateFunction> driver_list_;
            std::vector<BaseIoAdapter*> driver_instance_list_;
        };

        template<typename T, const char name[]>
        class IoAdapter : public BaseIoAdapter {
        public:
        
            static BaseIoAdapter* CreateModuleObject() {
                return new T();
            }

            constexpr uint32_t name_() override { return common::ConstCrc32(name); }

            struct FactoryEntry {
                FactoryEntry() {
                    ModuleFactory::instance_().Register(common::ConstCrc32(name), CreateModuleObject);
                }
                inline void DoNothing() const {}
            };

            inline static const FactoryEntry factory_entry_;

            IoAdapter() { factory_entry_.DoNothing(); }
            virtual ~IoAdapter() { factory_entry_.DoNothing(); }
        };

        // template<typename T, const char name[]>
        // inline const typename IoAdapter<T, name>::FactoryEntry IoAdapter<T, name>::factory_entry_;
    }
}

#endif //OMNISTACK_IO_IO_ADAPTER_H
