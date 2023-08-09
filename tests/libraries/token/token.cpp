#include <gtest/gtest.h>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <sys/time.h>
#include <thread>

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

    usleep(2000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        try {
            omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
                , true
#endif
            );
            omnistack::memory::InitializeSubsystemThread();
            omnistack::token::InitializeSubsystem();
        } catch (std::runtime_error& err_info) {
            std::cerr << err_info.what() << std::endl;
            exit(1);
        }
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

    usleep(2000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        try {
            omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
                , true
#endif
            );
            omnistack::memory::InitializeSubsystemThread();
            omnistack::token::InitializeSubsystem();

            auto token = omnistack::token::CreateToken();
            if (token->CheckToken())
                exit(1);
            token->AcquireToken();
            if (!token->CheckToken())
                exit(1);
        } catch (std::runtime_error& err_info) {
            std::cerr << err_info.what() << std::endl;
            exit(1);
        }
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

TEST(LibrariesToken, AutoTakeTokenBack) {
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

    usleep(2000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        try {
            omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
                , true
#endif
            );
            omnistack::memory::InitializeSubsystemThread();
            omnistack::token::InitializeSubsystem();

            auto token = omnistack::token::CreateToken();
            if (token->CheckToken())
                exit(1);

            token->AcquireToken();
            if (!token->CheckToken())
                exit(1);
            printf("%lu equal to %lu\n", omnistack::memory::thread_id, token->token);
            auto new_thread = std::thread([&]() {
                omnistack::memory::InitializeSubsystemThread();
                struct timeval start_time, end_time;
                gettimeofday(&start_time, NULL);
                token->AcquireToken();
                if (!token->CheckToken())
                    exit(1);
                gettimeofday(&end_time, NULL);
                auto start_tick = start_time.tv_sec * 1000000ul + start_time.tv_usec;
                auto end_tick = end_time.tv_sec * 1000000ul + end_time.tv_usec;
                printf("Get Token in %lu us\n", end_tick - start_tick);
                printf("%lu equal to %lu\n", omnistack::memory::thread_id, token->token);
            });
            new_thread.join();
            printf("%lu not equal to %lu\n", omnistack::memory::thread_id, token->token);
            if (token->CheckToken())
                exit(1);
        } catch (std::runtime_error& err_info) {
            std::cerr << err_info.what() << std::endl;
            exit(1);
        }
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

TEST(LibrariesToken, ManuallyReturnToken) {
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

    usleep(2000000);

    auto pid_client = fork();
    if (pid_client == 0) {
        try {
            omnistack::memory::InitializeSubsystem(0
#if defined(OMNIMEM_BACKEND_DPDK)
                , true
#endif
            );
            omnistack::memory::InitializeSubsystemThread();
            omnistack::token::InitializeSubsystem();

            auto token = omnistack::token::CreateToken();
            if (token->CheckToken())
                exit(1);

            token->AcquireToken();
            if (!token->CheckToken())
                exit(1);
            printf("%lu equal to %lu\n", omnistack::memory::thread_id, token->token);
            auto new_thread = std::thread([&]() {
                omnistack::memory::InitializeSubsystemThread();
                struct timeval start_time, end_time;
                gettimeofday(&start_time, NULL);
                token->AcquireToken();
                if (!token->CheckToken())
                    exit(1);
                gettimeofday(&end_time, NULL);
                auto start_tick = start_time.tv_sec * 1000000ul + start_time.tv_usec;
                auto end_tick = end_time.tv_sec * 1000000ul + end_time.tv_usec;
                printf("Get Token in %lu us\n", end_tick - start_tick);
                printf("%lu equal to %lu\n", omnistack::memory::thread_id, token->token);
            });
            printf("%lu not equal to %lu\n", omnistack::memory::thread_id, token->token);
            int cnt = 0;
            while (token->CheckToken()) {
                usleep(1);
                cnt ++;
            }
            if (cnt >= 500000)
                exit(1);
            new_thread.join();
            printf("Return token in %d us\n", cnt);
        } catch (std::runtime_error& err_info) {
            std::cerr << err_info.what() << std::endl;
            exit(1);
        }
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