//
// Created by liuhao on 23-8-11.
//

#ifndef OMNISTACK_TCP_COMMON_TCP_CONGESTION_CONTROL_HPP
#define OMNISTACK_TCP_COMMON_TCP_CONGESTION_CONTROL_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <map>

namespace omnistack::data_plane::tcp_common {

    class TcpFlow;

    class TcpCongestionControlBase {
    public:
        TcpCongestionControlBase(TcpFlow* flow) : flow_(flow) {}
        TcpCongestionControlBase(const TcpCongestionControlBase&) = delete;
        TcpCongestionControlBase(TcpCongestionControlBase&&) = delete;
        virtual ~TcpCongestionControlBase() {}

        virtual void OnPacketAcked(uint32_t ack_bytes) {};

        virtual void OnPacketSent(uint32_t bytes) {};

        virtual void OnCongestionEvent() {};

        virtual void OnRetransmissionTimeout() {};

        virtual uint32_t GetCongestionWindow() const { return 0; };

        virtual uint32_t GetBytesCanSend() const { return 0; };

        virtual constexpr std::string_view name_() { return "TcpCongestionControlBase"; }
    
    protected:
        TcpFlow* flow_;
    };

    class TcpCongestionControlFactory {
    public:
        typedef std::function<TcpCongestionControlBase*(TcpFlow*)> CreateFunction;

        static TcpCongestionControlFactory& instance_() {
            static TcpCongestionControlFactory factory;
            return factory;
        }

        void Register(const std::string& name, const CreateFunction& func) {
            if(congestion_control_algorithms_.find(name) != congestion_control_algorithms_.end()) {
                /* TODO: report error */
                return;
            }
            if(func == nullptr) {
                /* TODO: report error */
                return;
            }
            if(!congestion_control_algorithms_.insert(std::make_pair(name, func)).second) {
                /* TODO: report error */
                return;
            }
        }

        [[nodiscard]] TcpCongestionControlBase* Create(const std::string& name, TcpFlow* flow) const {
            auto it = congestion_control_algorithms_.find(name);
            if(it == congestion_control_algorithms_.end()) return nullptr;
            return it->second(flow);
        }

    private:
        std::map<std::string, CreateFunction> congestion_control_algorithms_;
    };

    template<typename T, const char name[]>
    class TcpCongestionControl : public TcpCongestionControlBase {
    public:
        static TcpCongestionControlBase* CreateCongestionControlObject(TcpFlow* flow) {
            return new T(flow);
        }

        constexpr std::string_view name_() override { return std::string_view(name); }

        struct FactoryEntry {
            FactoryEntry() {
                TcpCongestionControlFactory::instance_().Register(std::string(name), CreateCongestionControlObject);
            }
            inline void DoNothing() const {}
        };

        static const FactoryEntry factory_entry_;

        TcpCongestionControl(TcpFlow* flow) : TcpCongestionControlBase(flow) {
            factory_entry_.DoNothing();
        }
        virtual ~TcpCongestionControl() {
            factory_entry_.DoNothing();
        }
    };

    template<typename T, const char name[]>
    typename TcpCongestionControl<T, name>::FactoryEntry const TcpCongestionControl<T, name>::factory_entry_;

}

#endif //OMNISTACK_TCP_COMMON_TCP_CONGESTION_CONTROL_HPP
