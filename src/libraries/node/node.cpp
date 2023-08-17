#include <omnistack/node.h>

namespace omnistack::node {
    constexpr int kCacheNodes = 128;
    static thread_local BasicNode** usable_basic_nodes;
    static thread_local BasicNode* recycled_basic_nodes[kCacheNodes];
    static thread_local uint32_t usable_basic_nodes_idx = kCacheNodes;

    void Connect(BasicNode* basic_node, EventNode* event_node) {
        basic_node->enode_ = memory::Pointer(event_node);
    }

    BasicNode* CreateBasicNodeNode() {
        if (usable_basic_nodes_idx == kCacheNodes) {
            constexpr int aligned_size = (sizeof(BasicNode*) + 63) / 64 * 64;
            constexpr int alloc_size = aligned_size * kCacheNodes;
            usable_basic_nodes = reinterpret_cast<BasicNode**>(
                memory::AllocateNamedShared("", alloc_size)
            );
            usable_basic_nodes_idx = 0;
        }
    }
}