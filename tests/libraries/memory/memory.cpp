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
        usleep(1000000);
        exit(0);
    }
    usleep(1000);
    auto client_pid = fork();
    if (!client_pid) {
        omnistack::memory::InitializeSubsystem();
        omnistack::memory::InitializeSubsystemThread();
        exit(0);
    }
    int status;
    waitpid(client_pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_pid, &status, 0);
}