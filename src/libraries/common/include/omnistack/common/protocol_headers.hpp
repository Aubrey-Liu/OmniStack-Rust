// 使用库里的头不太方便，自己实现一个
#ifndef __OMNISTACK_COMMON_PROTOCOL_HEADERS_HPP
#define __OMNISTACK_COMMON_PROTOCOL_HEADERS_HPP

#include <cstdint>
#include <arpa/inet.h>

namespace omnistack::common {

#pragma pack(1)
struct EthernetHeader {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
};

#ifndef BIGENDIAN
#define ETH_PROTO_TYPE_IPV4 0x0008
#define ETH_PROTO_TYPE_IPV6 0xDD86
#define ETH_PROTO_TYPE_ARP 0x0608
#else
#define ETH_PROTO_TYPE_IPV4 0x0800
#define ETH_PROTO_TYPE_IPV6 0x86DD
#define ETH_PROTO_TYPE_ARP 0x0806
#endif

#ifndef BIGENDIAN
#define ARP_PROTO_HW_ETH 0x0100
#define ARP_PROTO_OP_REQUEST 0x0100
#define ARP_PROTO_OP_REPLY 0x0200
#else
#define ARP_PROTO_HW_ETH 0x0001
#define ARP_PROTO_OP_REQUEST 0x0001
#define ARP_PROTO_OP_REPLY 0x0002
#endif
#define ARP_PROTO_PACKET_SIZE 20
struct ArpHeader {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t mac_length;
    uint8_t ip_length;
    uint16_t op_type;
};

struct ArpBody {
    uint8_t src_mac[6];
    uint32_t src_ip;
    uint8_t dst_mac[6];
    uint32_t dst_ip;
};

#pragma pack(1)
struct Ipv4Header {
#ifndef BIGENDIAN
    uint8_t     ihl: 4;
    uint8_t     version: 4;
#else
    uint8_t     version: 4;
    uint8_t     ihl: 4;
#endif
    uint8_t     tos;
    uint16_t    len;
    uint16_t    id;
    uint16_t    frag_off;
    uint8_t     ttl;
    uint8_t     proto;
    uint16_t    chksum;
    uint32_t    src;
    uint32_t    dst;
};

// If you wonder why I wrote like this, you should see how linux define it.
#pragma pack(1)
struct Ipv6Header {
#ifndef BIGENDIAN
	uint8_t		priority:4,
				version:4;
#else
	uint8_t		version:4,
				priority:4;
#endif
	uint8_t		fl[3];
	uint16_t	plen;
	uint8_t		nh;
	uint8_t		hlim;
    __uint128_t src;
    __uint128_t dst;
};

#define IP_PROTO_TYPE_TCP 0x06
#define IP_PROTO_TYPE_UDP 0x11
#define IP_PROTO_TYPE_EPOLL 144

#define TCP_OPTION_KIND_EOL 0x00
#define TCP_OPTION_KIND_NOP 0x01
#define TCP_OPTION_KIND_MSS 0x02
#define TCP_OPTION_KIND_WSOPT 0x03
#define TCP_OPTION_KIND_SACK_PREMITTED 0x04
#define TCP_OPTION_KIND_SACK 0x05
#define TCP_OPTION_KIND_TSPOT 0x08

#define TCP_OPTION_LENGTH_EOL 0x01
#define TCP_OPTION_LENGTH_NOP 0x01
#define TCP_OPTION_LENGTH_MSS 0x04
#define TCP_OPTION_LENGTH_WSOPT 0x03
#define TCP_OPTION_LENGTH_SACK_PREMITTED 0x02
#define TCP_OPTION_LENGTH_TSPOT 0x0A

#ifndef BIGENDIAN
    #define TCP_FLAGS_FIN 0x100
    #define TCP_FLAGS_SYN 0x200
    #define TCP_FLAGS_RST 0x400
    #define TCP_FLAGS_PSH 0x800
    #define TCP_FLAGS_ACK 0x1000
    #define TCP_FLAGS_URG 0x2000
    #define TCP_FLAGS_ECE 0x4000
    #define TCP_FLAGS_CWR 0x8000
#else
    #define TCP_FLAGS_FIN 0x01
    #define TCP_FLAGS_SYN 0x02
    #define TCP_FLAGS_RST 0x04
    #define TCP_FLAGS_PSH 0x08
    #define TCP_FLAGS_ACK 0x10
    #define TCP_FLAGS_URG 0x20
    #define TCP_FLAGS_ECE 0x40
    #define TCP_FLAGS_CWR 0x80
#endif

struct TcpHeader {
    uint16_t    sport;
    uint16_t    dport;
    uint32_t    seq;
    uint32_t    ACK;
    union {
    uint16_t    tcpflags;
    struct {
#ifndef BIGENDIAN
    
    uint16_t    reserved:4;
    uint16_t    dataofs:4;
    uint16_t    fin:1;
    uint16_t    syn:1;
    uint16_t    rst:1;
    uint16_t    psh:1;
    uint16_t    ack:1;
    uint16_t    urg:1;
    uint16_t    ece:1;
    uint16_t    cwr:1;
#else
    uint16_t    dataofs:4;
    uint16_t    reserved:4;
    uint16_t    cwr:1;
    uint16_t    ece:1;
    uint16_t    urg:1;
    uint16_t    ack:1;
    uint16_t    psh:1;
    uint16_t    rst:1;
    uint16_t    syn:1;
    uint16_t    fin:1;
#endif
    };
    };
    uint16_t    window;
    uint16_t    chksum;
    uint16_t    urgptr;
};

struct UdpHeader {
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t chksum;
};
#pragma pack()

}

#endif