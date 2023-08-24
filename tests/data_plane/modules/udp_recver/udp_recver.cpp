//
// Created by zengqihuan on 2023-8-23
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>
#include <omnistack/node.h>
#include <arpa/inet.h>
#include <omnistack/common/protocol_headers.hpp>


TEST(DataPlaneUdpRecver, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_udp_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneUdpRecver, Create) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_udp_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto udp_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("UdpRecver"));
    ASSERT_NE(udp_recver, nullptr);
    dlclose(handle);
}

TEST(DataPlaneUdpRecver, Functions) {
    using namespace omnistack::data_plane;
    using namespace omnistack::packet;
    using namespace omnistack::node;
    auto handle = dlopen("../lib/libomni_data_plane_udp_recver.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto udp_recver = ModuleFactory::instance_().Create(omnistack::common::ConstCrc32("UdpRecver"));
    ASSERT_NE(udp_recver, nullptr);
    Packet packet_ipv4 = Packet();
    Ipv4Header* ipv4_header = reinterpret_cast<Ipv4Header*>(packet_ipv4.data_ + packet_ipv4.offset_);
    auto& ipv4 = packet_ipv4.packet_headers_[packet_ipv4.header_tail_ ++];
    ipv4.length_ = 20;
    ipv4.offset_ = packet_ipv4.offset_;
    packet_ipv4.length_ = ntohs(ipv4_header->len) + packet_ipv4.offset_;
    packet_ipv4.offset_ += ipv4.length_;
    ipv4_header->version = 4;
    ipv4_header->proto = IP_PROTO_TYPE_UDP;
    auto result = udp_recver->DefaultFilter(&packet_ipv4);
    ASSERT_EQ(result, true);
    Packet packet_ipv5 = Packet();
    Ipv4Header* ipv5_header = reinterpret_cast<Ipv4Header*>(packet_ipv5.data_ + packet_ipv5.offset_);
    auto& ipv5 = packet_ipv5.packet_headers_[packet_ipv5.header_tail_ ++];
    ipv5.length_ = 20;
    ipv5.offset_ = packet_ipv5.offset_;
    packet_ipv5.length_ = ntohs(ipv5_header->len) + packet_ipv5.offset_;
    packet_ipv5.offset_ += ipv5.length_;
    ipv4_header->version = 5;
    ipv4_header->proto = IP_PROTO_TYPE_UDP;
    result = udp_recver->GetFilter(0, 0)(&packet_ipv5);
    ASSERT_EQ(result, false);
    result = udp_recver->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = udp_recver->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadWrite);
    dlclose(handle);
}
