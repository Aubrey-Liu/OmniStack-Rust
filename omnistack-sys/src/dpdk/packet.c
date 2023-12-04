#include <numa.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define NUM_MBUFS 4095
#define MBUF_CACHE_SIZE 250

struct rte_mempool *pktpool_create(const char *name)
{
    struct rte_mempool *mempool;

    mempool = rte_mempool_lookup(name);
    if (!mempool)
    {
        // todo: set socket id
        mempool = rte_pktmbuf_pool_create(name, NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                                          RTE_MBUF_DEFAULT_BUF_SIZE, 0);
    }

    return mempool;
}

struct rte_mbuf *pktpool_alloc(struct rte_mempool *mp)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(mp);

    return m;
}

void pktpool_dealloc(struct rte_mbuf *m)
{
    rte_pktmbuf_free(m);
}
