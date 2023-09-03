//
// Created by zengqihuan on 2023-8-18
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/node.h>
#include <arpa/inet.h>
#include <omnistack/common/protocol_headers.hpp>


TEST(DataPlaneIpv4Sender, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, Create) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, Functions) {
    using namespace omnistack::data_plane;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.transport_layer_type = TransportLayerType::kTCP;
    auto result = ipv4_sender->DefaultFilter(&pack);
    ASSERT_EQ(result, true);
    node.info_.transport_layer_type = TransportLayerType::kUDP;
    result = ipv4_sender->GetFilter(0, 0)(&pack);
    ASSERT_EQ(result, true);
    result = ipv4_sender->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = ipv4_sender->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, Logfile) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    ipv4_sender->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, MainLogicFoundRoute) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.network.ipv4.sip = 1;
    node.info_.network.ipv4.dip = 2;
    auto res = ipv4_sender->MainLogic(&pack);
    ASSERT_NE(res, nullptr);
    ipv4_sender->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, MainLogicFoundNoRoute) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.network.ipv4.sip = 1;
    node.info_.network.ipv4.dip = -1;
    auto res = ipv4_sender->MainLogic(&pack);
    ASSERT_EQ(res, nullptr);
    ipv4_sender->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, MainLogicHugePacket) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    pack.length_ = 1481;
    auto res = ipv4_sender->MainLogic(&pack);
    ASSERT_EQ(res, nullptr);
    ipv4_sender->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Sender, MainLogicPacketModify) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.network.ipv4.sip = htons(1);
    node.info_.network.ipv4.dip = htons(2);
    uint16_t old_length = pack.length_;
    auto res = ipv4_sender->MainLogic(&pack);
    ASSERT_NE(res, nullptr);
    ASSERT_EQ(old_length, res->GetLength() - sizeof(Ipv4Header));
    auto header = res->GetL3Header<Ipv4Header>();
    ASSERT_EQ(header->src, htons(1));
    ASSERT_EQ(header->dst, htons(2));
    ASSERT_EQ(header->len, htons(pack.length_));
    ipv4_sender->Destroy();
    dlclose(handle);
}