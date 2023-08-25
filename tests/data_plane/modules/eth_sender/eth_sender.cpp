//
// Created by zengqihuan on 2023-8-25
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>
#include <omnistack/node.h>

TEST(DataPlaneEthSender, Load)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_eth_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneEthSender, Create)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_eth_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthSender"));
    ASSERT_NE(eth_sender, nullptr);
    dlclose(handle);
}

TEST(DataPlaneEthSender, Functions)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_eth_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthSender"));
    ASSERT_NE(eth_sender, nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.network_layer_type = NetworkLayerType::kIPv4;
    auto result = eth_sender->DefaultFilter(&pack);
    ASSERT_EQ(result, true);
    node.info_.network_layer_type = NetworkLayerType::kIPv6;
    result = eth_sender->GetFilter(0, 0)(&pack);
    ASSERT_EQ(result, true);
    result = eth_sender->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = eth_sender->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneEthSender, MainLogic)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_eth_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthSender"));
    ASSERT_NE(eth_sender, nullptr);
    Packet pack = Packet();
    BasicNode node = BasicNode();
    pack.node_ = &node;
    node.info_.network_layer_type = NetworkLayerType::kIPv4;
    pack.nic_ = 1;
    auto res = eth_sender->MainLogic(&pack);
    auto header = reinterpret_cast<EthernetHeader*>(res->data_ + res->packet_headers_[res->header_tail_ - 1].offset_);
    ASSERT_EQ(header->dst[0], 1);
    ASSERT_EQ(header->src[0], 1);
    ASSERT_EQ(header->type, ETH_PROTO_TYPE_IPV4);
    dlclose(handle);
}