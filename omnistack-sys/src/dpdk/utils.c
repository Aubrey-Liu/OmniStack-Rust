#include <numa.h>

int node_of_cpu(int cpu) {
    return numa_node_of_cpu(cpu);
}
