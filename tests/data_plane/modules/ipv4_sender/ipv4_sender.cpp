//
// Created by zengqihuan on 2023-8-18
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>

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
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    auto result = ipv4_sender->DefaultFilter(nullptr);
    ASSERT_EQ(result, true);
    result = ipv4_sender->GetFilter(0, 0)(nullptr);
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

TEST(DataPlaneIpv4Sender, MainLogicExecOnce) {
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_sender.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_sender = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Sender"));
    ASSERT_NE(ipv4_sender, nullptr);
    ipv4_sender->Initialize("nothing", nullptr);
    Packet pack = Packet();
    pack.nic_ = 1;
    ipv4_sender->MainLogic(&pack);
    ipv4_sender->Destroy();
    dlclose(handle);
}