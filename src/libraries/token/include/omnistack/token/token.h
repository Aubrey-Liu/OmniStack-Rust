#ifndef OMNISTACK_TOKEN_H
#define OMNISTACK_TOKEN_H

#include <cstdint>
#include <omnistack/memory/memory.h>

namespace omnistack {
    namespace token {
        constexpr int kMaxControlPlane = 8;

        extern thread_local uint64_t thread_id;
        extern uint64_t process_id;

        enum class ControlPlaneStatus {
            kStarting = 0,
            kRunning,
            kStopped
        };

        enum class RpcRequestType {
            kReturn = 0,
            kAcquire,
            kCreateToken,
            kDestroyToken
        };

        enum class RpcResponseStatus {
            kSuccess = 0,
            kFail
        };

        class Token;
        void SendTokenMessage(RpcRequestType type, Token* token);

        class Token {
        public:
            Token();
            ~Token();
            
            uint64_t token_id;
            volatile uint64_t token;
            volatile uint64_t return_tick;

            inline bool CheckToken() {
                if (token != memory::thread_id) [[unlikely]]
                    return false;
                if (return_tick == 0) [[likely]]
                    return true;
                else if (return_tick == 1)
                    return false;
                else if (return_tick == 2) {
                    SendTokenMessage(RpcRequestType::kReturn, this);
                    return false;
                }
            }
            inline void AcquireToken() {
                if (CheckToken()) return;
                SendTokenMessage(RpcRequestType::kAcquire, this);
            }
        };
        
        /**
         * @brief This
        */
        void StartControlPlane();
        
        void InitializeSubsystem();

        void InitializeSubsystemThread();
    }
}

#endif
