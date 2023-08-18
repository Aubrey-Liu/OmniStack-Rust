//
// Created by zengqihuan on 2023-8-18
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/common/protocol_headers.hpp>

uint16_t compute_checksum(omnistack::common::Ipv4Header* header, uint16_t length);

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
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
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
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    Packet pack = Packet();
    (reinterpret_cast<EthernetHeader *>(pack.data_ + pack.packet_headers_[0].offset_))->type = ETH_PROTO_TYPE_IPV4;
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

TEST(DataPlaneIpv4Recver, Logfile)
{
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto ipv4_recver = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(ipv4_recver, nullptr);
    ipv4_recver->Initialize("nothing", nullptr);
    ipv4_recver->Destroy();
    dlclose(handle);
}

TEST(DataPlaneIpv4Recver, MainLogicExecOnce)
{
    using namespace omnistack::data_plane;
    using namespace omnistack::common;
    using namespace omnistack::packet;
    auto handle = dlopen("../lib/libomni_data_plane_ipv4_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto mod = ModuleFactory::instance_().Create("Ipv4Recver");
    ASSERT_NE(mod, nullptr);
    mod->Initialize("nothing", nullptr);
    Packet pack = Packet();
    Ipv4Header *header = reinterpret_cast<Ipv4Header *>(pack.data_ + pack.offset_);
    // edit a example pack
    header->ihl = 5; // default value without extra info
    header->version = 4;
    header->len = pack.length_ + (header->ihl << 2);
    header->tos = 0;
    header->id = 0;
    header->frag_off = 0;
    header->ttl = 10;
    header->proto = IP_PROTO_TYPE_TCP;
    header->chksum = 0;
    header->chksum = compute_checksum(header, header->ihl << 2);
    mod->MainLogic(&pack);
    mod->Destroy();
    dlclose(handle);
}

uint16_t compute_checksum(omnistack::common::Ipv4Header* header, uint16_t length)
{
    uint8_t* pos = (uint8_t*) header;
    uint32_t sum = 0;
    for(int i = 0;i < length;i += 2)
    {
        sum += *((uint16_t*)&pos[i]);
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum = (sum >> 16) + (sum & 0xffff);
    return (uint16_t)(~sum);
}