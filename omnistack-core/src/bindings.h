#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


#define PACKET_BUF_SIZE 1500

typedef struct Dpdk {

} Dpdk;

typedef struct CContext {
  const void *node;
  const void *tq;
} CContext;

typedef struct Packet {
  uint8_t buf[PACKET_BUF_SIZE];
  size_t refcnt;
} Packet;

typedef size_t NodeId;

typedef struct Task {
  struct Packet *data;
  NodeId node_id;
} Task;

extern void dpdk_init(struct Dpdk *module);

extern int dpdk_process(struct Dpdk *module, const struct CContext *ctx, struct Packet *packet);

extern int dpdk_tick(struct Dpdk *module, const struct CContext *ctx, struct Packet *packet);

void free_packet(struct Packet *self);

extern int numa_node_of_cpu(int cpu);

void push_task(const struct CContext *self, struct Task task);

void push_task_downstream(const struct CContext *self, struct Packet *data);

extern int rte_eal_init(int argc, const char *const *argv);

extern void rte_free(void *ptr);

extern void *rte_malloc_socket(const char *ty, size_t size, unsigned int align, int socket);
