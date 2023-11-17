#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PACKET_BUF_SIZE 1500

typedef struct Dpdk {
  void *mbuf_pool;
} Dpdk;

typedef struct CContext {
  const void *node;
  const void *tq;
} CContext;

typedef struct Packet {
  uint8_t buf[PACKET_BUF_SIZE];
  uintptr_t refcnt;
} Packet;

typedef uintptr_t NodeId;

typedef struct Task {
  struct Packet *data;
  NodeId node_id;
} Task;

extern int dpdk_init(struct Dpdk *module);

extern int dpdk_process(struct Dpdk *module, const struct CContext *ctx, struct Packet *packet);

extern int dpdk_tick(struct Dpdk *module, const struct CContext *ctx, struct Packet *packet);

void push_task_downstream(const struct CContext *self, struct Packet *data);

void push_task(const struct CContext *self, struct Task task);

void free_packet(struct Packet *self);
