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
        omnistack::memory::InitializeSubsystem(0, true);
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
        omnistack::memory::InitializeSubsystem(0, true);
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        usleep(1000000);
        exit(0);
    }
    auto client_pid1 = fork();
    if (!client_pid1) {
        omnistack::memory::InitializeSubsystem(0, true);
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
        omnistack::memory::InitializeSubsystem(0, true);
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
        omnistack::memory::InitializeSubsystem(0, true);
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
        omnistack::memory::InitializeSubsystem(0, true);
        printf("Connected to Control Plane\n");
        omnistack::memory::InitializeSubsystemThread();
        printf("Process id is %lu, Thread id is %lu\n", omnistack::memory::process_id, omnistack::memory::thread_id);
        auto shared_mem = (char*)omnistack::memory::AllocateNamedShared("Hello World", 1024);
        EXPECT_EQ(0, strcmp(shared_mem, "Hello World"));
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
