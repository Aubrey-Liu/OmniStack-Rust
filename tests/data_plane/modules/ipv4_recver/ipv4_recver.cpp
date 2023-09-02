//
// Created by zengqihuan on 2023-8-18
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

TEST(DataPlaneIpv4Recver, Load)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Create)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Functions)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    Packet pack = Packet();
    // check the Filter with pos and neg input
    pack.GetL2Header<EthernetHeader>()->type = ETH_PROTO_TYPE_IPV4;
    auto result = ipv4_recver->DefaultFilter(&pack);
    ASSERT_EQ(result, true);
    pack.GetL2Header<EthernetHeader>()->type = ~ETH_PROTO_TYPE_IPV4;
    result = ipv4_recver->GetFilter(0, 0)(&pack);
    ASSERT_EQ(result, false);
    result = ipv4_recver->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = ipv4_recver->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, Logfile)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicNormalPacket)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    Packet pack = Packet();
    Ipv4Header *header = reinterpret_cast<Ipv4Header *>(pack.data_ + pack.offset_);
    // edit a normal packet
    header->ihl = 5;
    header->version = 4;
    header->len = pack.length_ + (header->ihl << 2);
    header->tos = 0;
    header->id = 0;
    header->frag_off = 0;
    header->ttl = 10;
    header->proto = IP_PROTO_TYPE_TCP;
    header->chksum = 0;
    printf("packet ready\n");
    auto res = ipv4_recver->MainLogic(&pack);
    ASSERT_NE(res, nullptr);
    printf("normal packet passed\n");
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicIHLErrorPacket)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    Packet pack = Packet();
    Ipv4Header *header = reinterpret_cast<Ipv4Header *>(pack.data_ + pack.offset_);
    // edit a packet with ihl error
    header->ihl = 4;
    header->version = 4;
    header->len = pack.length_ + (header->ihl << 2);
    header->tos = 0;
    header->id = 0;
    header->frag_off = 0;
    header->ttl = 10;
    header->proto = IP_PROTO_TYPE_TCP;
    header->chksum = 0;
    printf("packet ready\n");
    auto res = ipv4_recver->MainLogic(&pack);
    ASSERT_EQ(res, nullptr);
    printf("ihl error packet droped\n");
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicTTLErrorPacket)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    Packet pack = Packet();
    Ipv4Header *header = reinterpret_cast<Ipv4Header *>(pack.data_ + pack.offset_);
    // edit a packet with ttl error
    header->ihl = 5;
    header->version = 4;
    header->len = pack.length_ + (header->ihl << 2);
    header->tos = 0;
    header->id = 0;
    header->frag_off = 0;
    header->ttl = 0;
    header->proto = IP_PROTO_TYPE_TCP;
    header->chksum = 0;
    printf("packet ready\n");
    auto res = ipv4_recver->MainLogic(&pack);
    ASSERT_EQ(res, nullptr);
    printf("ttl error packet droped\n");
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicBothErrorPacket)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("Ipv4Recver"));
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    Packet pack = Packet();
    Ipv4Header *header = reinterpret_cast<Ipv4Header *>(pack.data_ + pack.offset_);
    // edit a packet with ttl and ihl error
    header->ihl = 4;
    header->version = 4;
    header->len = pack.length_ + (header->ihl << 2);
    header->tos = 0;
    header->id = 0;
    header->frag_off = 0;
    header->ttl = 0;
    header->proto = IP_PROTO_TYPE_TCP;
    header->chksum = 0;
    printf("packet ready\n");
    auto res = ipv4_recver->MainLogic(&pack);
    ASSERT_EQ(res, nullptr);
    printf("ihl & ttl error packet droped\n");
    ipv4_recver->Destroy();
    dlclose(handle);
}