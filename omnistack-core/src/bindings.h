#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#define PACKET_BUF_SIZE 1500

typedef struct PacketPool {
  struct rte_mempool *mempool;
} PacketPool;

typedef struct Context {
  const void *node;
  const void *tq;
  struct PacketPool pktpool;
} Context;

typedef struct Packet {
  uint8_t buf[PACKET_BUF_SIZE];
  uintptr_t refcnt;
  struct rte_mbuf *mbuf;
} Packet;

typedef uintptr_t NodeId;

typedef struct Task {
  struct Packet *data;
  NodeId node_id;
} Task;

void push_task_downstream(const struct Context *self, struct Packet *data);

void push_task(const struct Context *self, struct Task task);

struct Packet *packet_alloc(const struct Context *self);

void packet_dealloc(const struct Context *self, struct Packet *packet);
