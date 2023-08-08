#include <gtest/gtest.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <sys/time.h>

TEST(LibrariesToken, Basic) {
    EXPECT_EQ(1, 1);
}

TEST(LibrariesToken, RunControlPlane) {
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
        usleep(10000);
        if (omnistack::token::GetControlPlaneStatus() != omnistack::token::ControlPlaneStatus::kRunning)
            exit(1);
        exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesToken, ConnectToControlPlane) {
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
        usleep(10000);
        if (omnistack::token::GetControlPlaneStatus() != omnistack::token::ControlPlaneStatus::kRunning)
            exit(1);
        usleep(3000000);
        exit(0);
    }

    usleep(1000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        omnistack::memory::InitializeSubsystem();
        omnistack::memory::InitializeSubsystemThread();
        omnistack::token::InitializeSubsystem();
        exit(0);
    }
    
    int status;
    waitpid(pid_client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibrariesToken, CreateToken) {
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
        usleep(10000);
        if (omnistack::token::GetControlPlaneStatus() != omnistack::token::ControlPlaneStatus::kRunning)
            exit(1);
        usleep(3000000);
        exit(0);
    }

    usleep(1000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        omnistack::memory::InitializeSubsystem();
        omnistack::memory::InitializeSubsystemThread();
        omnistack::token::InitializeSubsystem();

        auto token = omnistack::token::CreateToken();
        exit(0);
    }
    
    int status;
    waitpid(pid_client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(pid, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}