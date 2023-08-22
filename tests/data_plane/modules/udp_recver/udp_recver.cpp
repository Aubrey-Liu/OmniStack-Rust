//
// Created by zengqihuan on 2023-8-23
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
    //ASSERT_EQ(result, true);
    //node.info_.transport_layer_type = TransportLayerType::kUDP;
    //result = ipv4_sender->GetFilter(0, 0)(&pack);
    //ASSERT_EQ(result, true);
    result = ipv4_sender->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = ipv4_sender->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}
