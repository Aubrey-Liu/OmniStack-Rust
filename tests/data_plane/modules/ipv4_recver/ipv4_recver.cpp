//
// Created by zengqihuan on 2023-8-18
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>


TEST(DataPlaneIpv4Recver, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Create) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Functions) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    Packet pack = Packet();
    (reinterpret_cast<EthernetHeader*>(pack.data_ + pack.packet_headers_[0].offset_))->type = ETH_PROTO_TYPE_IPV4;
    auto result = ipv4_recver->DefaultFilter(&pack);
    ASSERT_EQ(result, true);
    result = ipv4_recver->GetFilter("upstream_module", 0)(&pack);
    ASSERT_EQ(result, true);
    result = ipv4_recver->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = ipv4_recver->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Logfile) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicExecOnce) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    Packet pack = Packet();
    pack.nic_ = 1;
    ipv4_recver->MainLogic(&pack);
    ipv4_recver->Destroy();
    dlclose(handle);
}