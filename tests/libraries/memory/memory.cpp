#include <gtest/gtest.h>
#include <omnistack/memory/memory.h>
#include <sys/time.h>

TEST(LibrariesMemory, Basic) {
    EXPECT_EQ(1, 1);
}

TEST(LibrariesMemory, RunControlPlane) {
    auto pid = fork();
    if (pid == 0) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(1000);
        if (omnistack::memory::GetControlPlaneStatus() != omnistack::memory::ControlPlaneStatus::kRunning)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesMemory, ConnectToControlPlane) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid = fork();
    if (!client_pid) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        exit(0);
    }
    int status;
    waitpid(client_pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, MultiConnectToControlPlane) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        usleep(1000000);
        exit(0);
    }
    auto client_pid1 = fork();
    if (!client_pid1) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        usleep(1000000);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(client_pid1, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, AllocateLocal) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid = fork();
    if (!client_pid) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);

        auto mem = (char*)omnistack::memory::AllocateLocal(1024);
        strcpy(mem, "Hello World");
        if (memcmp(mem, "Hello World", 11) != 0)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(client_pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, AllocateShared) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto shared_mem = (char*)omnistack::memory::AllocateNamedShared("Hello World", 1024);
        strcpy(shared_mem, "Hello World");
        usleep(1000000);
        exit(0);
    }
    auto client_pid1 = fork();
    if (!client_pid1) {
        usleep(500000);
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto shared_mem = (char*)omnistack::memory::AllocateNamedShared("Hello World", 1024);
        if (strcmp(shared_mem, "Hello World") != 0)
            exit(1);
        printf("Checked Shared Memory: %s\n", shared_mem);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(client_pid1, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, FreeShared) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto shared_mem = (char*)omnistack::memory::AllocateNamedShared("Hello World", 1024);
        strcpy(shared_mem, "Hello World");
        usleep(1000000);
        omnistack::memory::FreeNamedShared(shared_mem);
        exit(0);
    }
    auto client_pid1 = fork();
    if (!client_pid1) {
        usleep(500000);
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto shared_mem = (char*)omnistack::memory::AllocateNamedShared("Hello World", 1024);
        if (strcmp(shared_mem, "Hello World") != 0)
            exit(1);
        printf("Checked Shared Memory: %s\n", shared_mem);
        omnistack::memory::FreeNamedShared(shared_mem);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(client_pid1, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, CreateMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        if (mempool->chunk_count_ != 1024)
            exit(2);
        if (mempool == nullptr)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, GetChunkFromMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        auto chunk = mempool->Get();
        strcpy((char*)chunk, "Hello World");
        if (strcmp((char*)chunk, "Hello World") != 0)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, PutChunkToMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        auto chunk = mempool->Get();
        strcpy((char*)chunk, "Hello World");
        mempool->Put(chunk);
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, GetChunksFromMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        typeof(mempool->Get()) chunks[512];
        for (int i = 0; i < 512; i ++) {
            chunks[i] = mempool->Get();
            sprintf((char*)chunks[i], "Hello World %d", i);
        }
        for (int i = 0; i < 512; i ++) {
            if (strcmp((char*)chunks[i], ("Hello World " + std::to_string(i)).c_str()) != 0)
                exit(i + 1);
        }
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, PutChunksToMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        typeof(mempool->Get()) chunks[259];
        for (int i = 0; i < 259; i ++) {
            chunks[i] = mempool->Get();
            sprintf((char*)chunks[i], "Hello World %d", i);
        }
        for (int i = 0; i < 259; i ++) {
            mempool->Put(chunks[i]);
        }
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}

TEST(LibrariesMemory, MixedMempool) {
    auto control_pid = fork();
    if (!control_pid) {
        omnistack::memory::StartControlPlane(
    #if defined(OMNIMEM_BACKEND_DPDK)
            true
    #endif
        );
        usleep(3000000);
        exit(0);
    }
    usleep(2000000);
    auto client_pid0 = fork();
    if (!client_pid0) {
        omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
        , true
#endif
        );
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto mempool = omnistack::memory::AllocateMemoryPool("Hello World", 1024, 1024);
        typeof(mempool->Get()) chunks[259];
        for (int T = 0; T < 10; T ++) {
            for (int i = 0; i < 259; i ++) {
                chunks[i] = mempool->Get();
                sprintf((char*)chunks[i], "Hello World %d", i);
            }
            for (int i = 0; i < 259; i ++) {
                mempool->Put(chunks[i]);
            }
        }
        exit(0);
    }
    int status;
    waitpid(client_pid0, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}
