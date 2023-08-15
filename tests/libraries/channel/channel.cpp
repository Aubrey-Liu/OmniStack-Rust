#include <gtest/gtest.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <omnistack/channel/channel.h>
#include <sys/time.h>

TEST(LibrariesChannel, Basic) {
    EXPECT_EQ(1, 1);
}

TEST(LibrariesChannel, RunControlPlane) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(10000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        omnistack::memory::InitializeSubsystem();
        omnistack::token::StartControlPlane();
        omnistack::token::InitializeSubsystem();
        omnistack::channel::StartControlPlane();
        usleep(10000);
        if (omnistack::channel::GetControlPlaneStatus() != omnistack::channel::ControlPlaneStatus::kRunning)
            exit(2);
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesChannel, CreateChannel) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(10000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        omnistack::memory::InitializeSubsystem();
        omnistack::token::StartControlPlane();
        omnistack::token::InitializeSubsystem();
        omnistack::channel::StartControlPlane();
        omnistack::channel::InitializeSubsystem();
        auto channel = omnistack::channel::GetChannel("test");
        if (!channel)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesChannel, Transfer) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(10000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        omnistack::memory::InitializeSubsystem();
        omnistack::token::StartControlPlane();
        omnistack::token::InitializeSubsystem();
        omnistack::channel::StartControlPlane();
        omnistack::channel::InitializeSubsystem();
        auto channel = omnistack::channel::GetChannel("test");
        if (!channel)
            exit(1);
        auto mempool = omnistack::memory::AllocateMemoryPool("test_pool", 1024, 1024);
        void* ptrs[omnistack::channel::kBatchSize];
        for (int i = 0; i < omnistack::channel::kBatchSize; i ++) {
            ptrs[i] = mempool->Get();
            if (!ptrs[i])
                exit(3);
            channel->Write(ptrs[i]);
        }
        for (int i = 0; i < omnistack::channel::kBatchSize; i ++) {
            auto ret = channel->Read();
            if (ret != ptrs[i])
                exit(4);
        }
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesChannel, Flush) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(10000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        omnistack::memory::InitializeSubsystem();
        omnistack::token::StartControlPlane();
        omnistack::token::InitializeSubsystem();
        omnistack::channel::StartControlPlane();
        omnistack::channel::InitializeSubsystem();
        auto channel = omnistack::channel::GetChannel("test");
        if (!channel)
            exit(1);
        auto mempool = omnistack::memory::AllocateMemoryPool("test_pool", 1024, 1024);
        void* ptrs[omnistack::channel::kBatchSize];
        for (int i = 0; i < omnistack::channel::kBatchSize - 1; i ++) {
            ptrs[i] = mempool->Get();
            if (!ptrs[i])
                exit(3);
            channel->Write(ptrs[i]);
        }
        channel->Flush();
        for (int i = 0; i < omnistack::channel::kBatchSize - 1; i ++) {
            auto ret = channel->Read();
            if (ret != ptrs[i])
                exit(4);
        }
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesChannel, Batch) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(10000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        omnistack::memory::InitializeSubsystem();
        omnistack::token::StartControlPlane();
        omnistack::token::InitializeSubsystem();
        omnistack::channel::StartControlPlane();
        omnistack::channel::InitializeSubsystem();
        auto channel = omnistack::channel::GetChannel("test");
        if (!channel)
            exit(1);
        auto mempool = omnistack::memory::AllocateMemoryPool("test_pool", 1024, 1024);
        void* ptrs[omnistack::channel::kBatchSize];
        for (int i = 0; i < omnistack::channel::kBatchSize - 1; i ++) {
            ptrs[i] = mempool->Get();
            if (!ptrs[i])
                exit(3);
            channel->Write(ptrs[i]);
        }
        if (channel->Read() != nullptr)
            exit(4);
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
