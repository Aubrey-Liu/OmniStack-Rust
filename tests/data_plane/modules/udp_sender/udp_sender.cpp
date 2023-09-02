//
// Created by zengqihuan on 2023-8-24
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/node.h>
#include <arpa/inet.h>
#include <omnistack/common/protocol_headers.hpp>


TEST(DataPlaneUdpSender, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_udp_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneUdpSender, Create) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_udp_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto udp_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("UdpSender"));
    ASSERT_NE(udp_sender, nullptr);
    dlclose(handle);
}

TEST(DataPlaneUdpSender, Functions) {
    using namespace omnistack::data_plane;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_udp_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto udp_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("UdpSender"));
    ASSERT_NE(udp_sender, nullptr);
    auto result = udp_sender->DefaultFilter(nullptr);
    ASSERT_EQ(result, true);
    result = udp_sender->GetFilter(0, 0)(nullptr);
    ASSERT_EQ(result, true);
    result = udp_sender->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = udp_sender->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneUdpSender, MainLogic) {
    using namespace omnistack::data_plane;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_udp_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto udp_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("UdpSender"));
    ASSERT_NE(udp_sender, nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.transport.udp.sport = htons(1);
    node.info_.transport.udp.dport = htons(2);
    auto res = udp_sender->MainLogic(&pack);
    ASSERT_NE(res, nullptr);
    auto header = res->GetL4Header<UdpHeader>();
    ASSERT_EQ(header->sport, htons(1));
    ASSERT_EQ(header->dport, htons(2));
    ASSERT_EQ(header->chksum, 0);
    ASSERT_EQ(header->len, htons(pack.length_));
    dlclose(handle);
}