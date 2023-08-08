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
            uint64_t token;
            uint64_t returning;
            bool need_return[memory::kMaxThread + 1];

            inline bool CheckToken() {
                if (token != memory::thread_id) [[unlikely]]
                    return false;
                if (need_return[memory::thread_id] == false) [[unlikely]] {
                    SendTokenMessage(RpcRequestType::kReturn, this);
                    return false;
                }
                return true;
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

        // void InitializeSubsystemThread();

        Token* CreateToken();

        ControlPlaneStatus GetControlPlaneStatus();
    }
}

#endif
