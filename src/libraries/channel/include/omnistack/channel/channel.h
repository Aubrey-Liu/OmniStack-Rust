
#ifndef OMNISTACK_CHANNEL_H
#define OMNISTACK_CHANNEL_H

#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>

namespace omnistack {
    namespace channel {
        enum class ControlPlaneStatus {
            kStarting = 0,
            kRunning,
            kStopped
        };

        constexpr int kChannelSize = 1024;
        constexpr int kBatchSize = 16;

        /* Channel can only be used to transfer data  */
        class Channel
        {
        public:
            /**
             * @brief Write data to channel
             * @param data Data to be written
             * @return 0 if success, 1 if flushed, -1 if failed
            */
            int Write(const void* data);

            /**
             * @brief Read data from channel
             * @return Data read from channel
            */
            void* Read();

            /**
             * @brief Flush channel from writer side
            */
            int Flush();

#if defined(OMNIMEM_BACKEND_DPDK)
            token::Token* reader_token_ptr_;
            token::Token* writer_token_ptr_;
#else
            uint64_t reader_token_offset_;
            uint64_t writer_token_offset_;
#endif
        
        private:
            char padding0_[64];

            volatile uint32_t write_pos_;
            volatile uint32_t read_pos_;

            char padding1_[64 - 2 * sizeof(uint32_t)];

            uint32_t writer_write_pos_;
            uint32_t writer_read_pos_;
            uint32_t writer_batch_count_;

            char padding2_[64 - 3 * sizeof(uint32_t)];

            uint32_t reader_write_pos_;
            uint32_t reader_read_pos_;
            uint32_t reader_batch_count_;

            char padding3_[64 - 3 * sizeof(uint32_t)];

#if defined(OMNIMEM_BACKEND_DPDK)
            memory::RegionMeta* ring_ptr_[kChannelSize];
#else
            uint64_t ring_offset_[kChannelSize];
#endif
        };

        class MultiWriterChannel {
        public:
            /**
             * @brief Write data to channel
             * @param data Data to be written
             * @return 0 if success, 1 if flushed, -1 if failed
            */
            int Write(const void* data);

            /**
             * @brief Read data from channel
             * @return Data read from channel
            */
            void* Read();

            /**
             * @brief Flush channel from writer side
            */
            void Flush();

            Channel* channels_[memory::kMaxThread + 1];

            int read_tick_[memory::kMaxThread * 2];
            int write_tick_[memory::kMaxThread];
        };

        void StartControlPlane();
        
        void InitializeSubsystem();

        Channel* CreateChannel();

        void DestroyChannel(Channel* channel);
    } // namespace channel
} // namespace omnistack


#endif //OMNISTACK_CHANNEL_H