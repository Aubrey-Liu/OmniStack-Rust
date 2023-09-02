//
// Created by zengqihuan on 2023-8-25
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

TEST(DataPlaneEthRecver, Load)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_eth_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneEthRecver, Create)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_eth_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthRecver"));
    ASSERT_NE(eth_recver, nullptr);
    dlclose(handle);
}

TEST(DataPlaneEthRecver, Functions)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_eth_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthRecver"));
    ASSERT_NE(eth_recver, nullptr);
    auto result = eth_recver->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = eth_recver->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneEthRecver, MainLogic)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_eth_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto eth_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("EthRecver"));
    ASSERT_NE(eth_recver, nullptr);
    Packet pack = Packet();
    EthernetHeader* eth_header = reinterpret_cast<EthernetHeader*>(pack.data_ + pack.offset_);
    eth_header->type = ETH_PROTO_TYPE_IPV4;
    eth_recver->MainLogic(&pack);
    ASSERT_EQ(pack.GetL2Header<EthernetHeader>()->type, ETH_PROTO_TYPE_IPV4);
    dlclose(handle);
}