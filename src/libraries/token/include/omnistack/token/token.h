#ifndef OMNISTACK_TOKEN_H
#define OMNISTACK_TOKEN_H

#include <cstdint>

namespace omnistack {
    namespace token {
        constexpr int kMaxControlPlane = 8;

        extern thread_local uint64_t thread_id;
        extern uint64_t process_id;

        class Token {
        public:
            Token();
            ~Token();
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
