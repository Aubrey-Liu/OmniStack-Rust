#include <omnistack/node.h>

namespace omnistack::node {
    constexpr int kCacheNodes = 128;
    constexpr int kMaxComUser = 16;

    static thread_local memory::Pointer<BasicNode> usable_basic_nodes[kCacheNodes];
    static thread_local memory::Pointer<EventNode> usable_event_nodes[kCacheNodes];
    static thread_local uint32_t usable_basic_nodes_idx = kCacheNodes;

    static thread_local BasicNode* local_recycled_basic_nodes = nullptr;
    static thread_local uint32_t num_local_recycled_basic_nodes = 0;

    static memory::Pointer<BasicNode*> recycled_basic_nodes;
    static memory::Pointer<EventNode*> recycled_event_nodes;
    static uint32_t* allocated_node_count;
    static pthread_mutex_t* node_lock;

    static pthread_mutex_t* node_lock_control_plane;
    static memory::Pointer<BasicNode*> recycled_basic_nodes_control_plane;
    static memory::Pointer<EventNode*> recycled_event_nodes_control_plane;
    static uint32_t* allocated_node_count_control_plane;

    static memory::Pointer<BasicNode> protocol_stack_nodes[kMaxComUser];

    void StartControlPlane(int num_com_user) {
        node_lock_control_plane = reinterpret_cast<typeof(node_lock_control_plane)>(
            memory::AllocateNamedShared("omni_node_lock", sizeof(pthread_mutex_t)));
        recycled_basic_nodes_control_plane = memory::Pointer(reinterpret_cast<BasicNode**>(
            memory::AllocateNamedShared("omni_recycled_basic_nodes", sizeof(BasicNode*))));
        recycled_event_nodes_control_plane = memory::Pointer(reinterpret_cast<EventNode**>(
            memory::AllocateNamedShared("omni_recycled_event_nodes", sizeof(EventNode*))));
        allocated_node_count_control_plane = reinterpret_cast<typeof(allocated_node_count_control_plane)>(
            memory::AllocateNamedShared("omni_allocated_node_count", sizeof(uint32_t)));
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(node_lock_control_plane, &attr);
        pthread_mutexattr_destroy(&attr);

        *allocated_node_count_control_plane = 0;

        *recycled_basic_nodes_control_plane = nullptr;
        *recycled_event_nodes_control_plane = nullptr;
    }

    void InitializeSubsystem() {
        node_lock = reinterpret_cast<typeof(node_lock)>(
            memory::AllocateNamedShared("omni_node_lock", sizeof(pthread_mutex_t)));
        recycled_basic_nodes = memory::Pointer(reinterpret_cast<BasicNode**>(
            memory::AllocateNamedShared("omni_recycled_basic_nodes", sizeof(BasicNode*))));
        recycled_event_nodes = memory::Pointer(reinterpret_cast<EventNode**>(
            memory::AllocateNamedShared("omni_recycled_event_nodes", sizeof(EventNode*))));
        allocated_node_count = reinterpret_cast<typeof(allocated_node_count)>(
            memory::AllocateNamedShared("omni_allocated_node_count", sizeof(uint32_t)));
    }

    void Connect(BasicNode* basic_node, EventNode* event_node) {
        basic_node->enode_ = memory::Pointer(event_node);
    }

    memory::Pointer<BasicNode> CreateBasicNodeNode() {
        if (usable_basic_nodes_idx == kCacheNodes) {
            pthread_mutex_lock(node_lock);
            for (int i = 0; i < kCacheNodes; i++) {
                if (*recycled_basic_nodes) {
                    auto cur = *recycled_basic_nodes;
                    (*recycled_basic_nodes) = cur->next_.Get();
                    cur->next_.Set(nullptr);
                    usable_basic_nodes[i] = cur;
                } else {
                    auto cur = reinterpret_cast<BasicNode*>(memory::AllocateNamedShared("", sizeof(BasicNode)));
                    cur->id_ = ++(*allocated_node_count);
                    cur->enode_.Set(nullptr);
                    cur->next_.Set(nullptr);
                    usable_basic_nodes[i] = cur;
                }
            }
            pthread_mutex_unlock(node_lock);
            usable_basic_nodes_idx = 0;
        }
        auto basic_node = usable_basic_nodes[usable_basic_nodes_idx++];
        basic_node->enode_ = nullptr;
        return basic_node;
    }

    void ReleaseBasicNode(BasicNode* node) {
        if (num_local_recycled_basic_nodes == kCacheNodes) {
            pthread_mutex_lock(node_lock);
            pthread_mutex_unlock(node_lock);
        }
        ++num_local_recycled_basic_nodes;
        node->next_ = memory::Pointer(local_recycled_basic_nodes);
        local_recycled_basic_nodes = node;
    }
}