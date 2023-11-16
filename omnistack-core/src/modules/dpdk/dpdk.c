#include "bindings.h"
#include <stdio.h>

int dpdk_process(Dpdk *dpdk, const CContext *ctx, Packet *packet)
{
    printf("dpdk received one packet\n");

    free_packet(packet);

    return 0;
}

