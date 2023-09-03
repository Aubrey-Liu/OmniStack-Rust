//
// Created by liuhao on 23-8-9.
//

#include <gtest/gtest.h>
#include <omnistack/packet/packet.hpp>
#include <omnistack/memory/memory.h>

TEST(LibPacket, Create) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibPacket, Allocate) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        auto packet = packet_pool->Allocate();
        if(packet == nullptr) {
            std::cerr << "Failed to allocate packet." << std::endl;
            exit(1);
        }
        packet_pool->Free(packet);
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibPacket, Release) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        auto packet = packet_pool->Allocate();
        if(packet == nullptr) {
            std::cerr << "Failed to allocate packet." << std::endl;
            exit(1);
        }
        packet->Release();
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibPacket, ReferenceCount) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        auto packet = packet_pool->Allocate();
        if(packet == nullptr) {
            std::cerr << "Failed to allocate packet." << std::endl;
            exit(1);
        }
        if(packet->reference_count_ != 1) {
            std::cerr << "Reference count is not 1." << std::endl;
            exit(1);
        }
        packet->reference_count_ = 2;
        if(packet->reference_count_ != 2) {
            std::cerr << "Reference count is not 2." << std::endl;
            exit(1);
        }
        packet->Release();
        if(packet->reference_count_ != 1) {
            std::cerr << "Reference count is not 1 after release." << std::endl;
            exit(1);
        }
        packet->Release();
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibPacket, Duplicate) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        auto packet = packet_pool->Allocate();
        if(packet == nullptr) {
            std::cerr << "Failed to allocate packet." << std::endl;
            exit(1);
        }
        sprintf(packet->GetPayload(), "%s", "Hello World!");
        packet->SetLength(strlen(packet->GetPayload()));
        packet->l2_header.offset_ = 0;
        packet->l2_header.length_ = packet->GetLength();
        auto packet2 = packet_pool->Duplicate(packet);
        if(packet2 == nullptr) {
            std::cerr << "Failed to duplicate packet." << std::endl;
            exit(1);
        }
        if(packet->reference_count_ != 1) {
            std::cerr << "Reference count is not 1." << std::endl;
            exit(1);
        }
        if(packet2->reference_count_ != 1) {
            std::cerr << "Reference count is not 1." << std::endl;
            exit(1);
        }
        if(strcmp(packet->GetPayload(), packet2->GetPayload())) {
            std::cerr << "Data is not the same." << std::endl;
            exit(1);
        }
        if(packet->GetLength() != packet2->GetLength()) {
            std::cerr << "Length is not the same." << std::endl;
            exit(1);
        }
        if(packet2->l2_header.offset_ != 0) {
            std::cerr << "Header tail data is not data." << std::endl;
            exit(1);
        }
        if(packet2->l2_header.length_ != packet->GetLength()) {
            std::cerr << "Header tail length is not data length." << std::endl;
            exit(1);
        }
        packet->Release();
        packet2->Release();
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LibPakcet, Reference) {
    auto control_plane = fork();
    if (control_plane == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::StartControlPlane(true);
#else
        omnistack::memory::StartControlPlane();
#endif
        usleep(2000000);
        exit(0);
    }
    usleep(1000000);
    auto client = fork();
    if (client == 0) {
#if defined (OMNIMEM_BACKEND_DPDK)
        omnistack::memory::InitializeSubsystem(0, true);
#else
        omnistack::memory::InitializeSubsystem(0);
#endif
        omnistack::memory::InitializeSubsystemThread();
        auto packet_pool = omnistack::packet::PacketPool::CreatePacketPool("test", 1024);
        if(packet_pool == nullptr) {
            std::cerr << "Failed to create packet pool." << std::endl;
            exit(1);
        }
        auto packet = packet_pool->Allocate();
        if(packet == nullptr) {
            std::cerr << "Failed to allocate packet." << std::endl;
            exit(1);
        }
        sprintf(packet->GetPayload(), "%s", "Hello World!");
        packet->SetLength(strlen(packet->GetPayload()));
        packet->l2_header.offset_ = 0;
        packet->l2_header.length_ = packet->GetLength();
        auto packet2 = packet_pool->Reference(packet);
        if(packet2 == nullptr) {
            std::cerr << "Failed to reference packet." << std::endl;
            exit(1);
        }
        if(packet->reference_count_ != 2) {
            std::cerr << "Reference count is not 2." << std::endl;
            exit(1);
        }
        if(packet2->reference_count_ != 1) {
            std::cerr << "Reference count is not 1." << std::endl;
            exit(1);
        }
        if(packet->GetPayload() != packet2->GetPayload()) {
            std::cerr << "Data is not the same." << std::endl;
            exit(1);
        }
        if(packet->GetLength() != packet2->GetLength()) {
            std::cerr << "Length is not the same." << std::endl;
            exit(1);
        }
        if(packet2->l2_header.offset_ != 0) {
            std::cerr << "Header tail data is not the same." << std::endl;
            exit(1);
        }
        if(packet2->l2_header.length_ != packet->GetLength()) {
            std::cerr << "Header tail length is not the same." << std::endl;
            exit(1);
        }
        packet->Release();
        if(packet->reference_count_ != 1) {
            std::cerr << "Reference count is not 1 after release." << std::endl;
            exit(1);
        }
        packet2->Release();
        omnistack::packet::PacketPool::DestroyPacketPool(packet_pool);
        exit(0);
    }
    int status;
    waitpid(client, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    waitpid(control_plane, &status, 0);
    EXPECT_EQ(bool(WIFEXITED(status)), true);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
