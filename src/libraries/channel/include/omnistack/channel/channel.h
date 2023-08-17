
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

        class RawChannel {
        public:
            void Init();

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
        
            bool IsReadable();
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

#if defined (OMNIMEM_BACKEND_DPDK)
            void* ring_ptr_[kChannelSize];
#else
            uint64_t ring_offset_[kChannelSize];
#endif

public:
            bool initialized_;
        };

        /* Channel can only be used to transfer data  */
        class Channel
        {
        public:
            void Init(RawChannel* raw_channel);

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

            memory::Pointer<token::Token> reader_token_;
            memory::Pointer<token::Token> writer_token_;
        
        private:
            memory::Pointer<RawChannel> raw_channel_;

        public:
            bool initialized_;
        };

        class MultiWriterChannel {
        public:
            void Init();

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

            memory::Pointer<token::Token> reader_token_;

            memory::Pointer<RawChannel> channel_ptrs_[memory::kMaxThread + 1];
            memory::Pointer<RawChannel> current_channel_ptr_;

            uint64_t read_tick_[memory::kMaxThread * 4];
            uint64_t write_tick_[memory::kMaxThread];
            uint64_t current_channel_thread_id_;

public:
            bool initialized;
            char name[128];
        };

        void StartControlPlane();
        
        void InitializeSubsystem();

        void ForkSubsystem();

        RawChannel* GetRawChannel(const std::string& name);

        Channel* GetChannel(const std::string& name);

        MultiWriterChannel* GetMultiWriterChannel();

        void DestroyChannel(Channel* channel);

        ControlPlaneStatus GetControlPlaneStatus();
    } // namespace channel
} // namespace omnistack


#endif //OMNISTACK_CHANNEL_H