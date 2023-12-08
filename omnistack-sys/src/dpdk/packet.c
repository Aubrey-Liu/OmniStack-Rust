#include <numa.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

struct rte_mempool *mempool_create(const char *name, unsigned int n, unsigned int elt_size,

                                   unsigned int cache_size, int socket_id)
{
    struct rte_mempool *mempool;

    mempool = rte_mempool_lookup(name);
    if (!mempool)
    {
        // todo: set socket id
        mempool = rte_mempool_create(name, n, elt_size, cache_size, 0,
                                     NULL, NULL, NULL, NULL, socket_id, 0);
    }

    return mempool;
}

void *mempool_get(struct rte_mempool *mp)
{
    void *obj = NULL;
    rte_mempool_get(mp, &obj);

    return obj;
}

void mempool_put(struct rte_mempool *mp, void *obj)
{
    rte_mempool_put(mp, obj);
}

struct rte_mempool *pktpool_create(const char *name, unsigned int n,
                                   unsigned int cache_size, int socket_id)
{
    struct rte_mempool *mempool;

    mempool = rte_mempool_lookup(name);
    if (!mempool)
    {
        // todo: set socket id
        mempool = rte_pktmbuf_pool_create(name, n, cache_size, 0,
                                          RTE_MBUF_DEFAULT_BUF_SIZE, socket_id);
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
