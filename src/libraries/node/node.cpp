#include <omnistack/node.h>
#include <stdexcept>
#include <omnistack/hashtable/hashtable.h>
#include <omnistack/common/logger.h>

namespace omnistack::node {
    constexpr int kCacheNodes = 128;
    constexpr int kMaxComUser = 16;

    static thread_local memory::Pointer<BasicNode> usable_basic_nodes[kCacheNodes];
    static thread_local memory::Pointer<EventNode> usable_event_nodes[kCacheNodes];
    static thread_local uint32_t usable_basic_nodes_idx = kCacheNodes;
    static thread_local uint32_t usable_event_nodes_idx = kCacheNodes;

    static thread_local BasicNode* local_recycled_basic_nodes = nullptr;
    static thread_local EventNode* local_recycled_event_nodes = nullptr;
    static thread_local uint32_t num_local_recycled_basic_nodes = 0;
    static thread_local uint32_t local_create_channel_idx = 0;

    static memory::Pointer<BasicNode*> recycled_basic_nodes;
    static memory::Pointer<EventNode*> recycled_event_nodes;
    static pthread_mutex_t* node_lock;

    static pthread_mutex_t* node_lock_control_plane;
    static memory::Pointer<BasicNode*> recycled_basic_nodes_control_plane;
    static memory::Pointer<EventNode*> recycled_event_nodes_control_plane;

    static channel::MultiWriterChannel* protocol_stack_nodes[kMaxComUser];
    static packet::PacketPool* temp_packet_pool;

    static int num_node_user;

    void StartControlPlane(int num_com_user) {
        node_lock_control_plane = reinterpret_cast<typeof(node_lock_control_plane)>(
            memory::AllocateNamedShared("omni_node_lock", sizeof(pthread_mutex_t)));
        recycled_basic_nodes_control_plane = memory::Pointer(reinterpret_cast<BasicNode**>(
            memory::AllocateNamedShared("omni_recycled_basic_nodes", sizeof(BasicNode*))));
        recycled_event_nodes_control_plane = memory::Pointer(reinterpret_cast<EventNode**>(
            memory::AllocateNamedShared("omni_recycled_event_nodes", sizeof(EventNode*))));
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(node_lock_control_plane, &attr);
        pthread_mutexattr_destroy(&attr);

        *recycled_basic_nodes_control_plane = nullptr;
        *recycled_event_nodes_control_plane = nullptr;

        auto num_core_user_p = reinterpret_cast<int*>(memory::AllocateNamedShared(
            "omni_node_num_core_user", sizeof(int)));
        *num_core_user_p = num_com_user;
    }

    void InitializeSubsystem() {
        node_lock = reinterpret_cast<typeof(node_lock)>(
            memory::AllocateNamedShared("omni_node_lock", sizeof(pthread_mutex_t)));
        recycled_basic_nodes = memory::Pointer(reinterpret_cast<BasicNode**>(
            memory::AllocateNamedShared("omni_recycled_basic_nodes", sizeof(BasicNode*))));
        recycled_event_nodes = memory::Pointer(reinterpret_cast<EventNode**>(
            memory::AllocateNamedShared("omni_recycled_event_nodes", sizeof(EventNode*))));
        
        auto num_core_user = *reinterpret_cast<int*>(memory::AllocateNamedShared(
            "omni_node_num_core_user", sizeof(int)));

        for (int i = 0; i < num_core_user; i ++) {
            auto stack_name = "omni_node_user_channel_" + std::to_string(i);
            protocol_stack_nodes[i] = 
                channel::GetMultiWriterChannel(stack_name);
            OMNI_LOG(common::kInfo) << "Node user " << i << " channel: " << stack_name << std::endl;
        }

        num_node_user = num_core_user;
        temp_packet_pool = packet::PacketPool::CreatePacketPool("omni_temp_packet_pool", 16384);
    }

    void BasicNode::Connect(EventNode* event_node) {
        this->enode_ = memory::Pointer(event_node);
    }

    BasicNode* CreateBasicNode(uint32_t com_user_id) {
        if (usable_basic_nodes_idx == kCacheNodes) {
            pthread_mutex_lock(node_lock);
            for (int i = 0; i < kCacheNodes; i++) {
                if (*recycled_basic_nodes) {
                    auto cur = *recycled_basic_nodes;
                    (*recycled_basic_nodes) = cur->GetNext().Get();
                    cur->Init();
                    usable_basic_nodes[i] = cur;
                } else {
                    auto cur = reinterpret_cast<BasicNode*>(memory::AllocateNamedShared("", sizeof(BasicNode)));
                    cur->Init();
                    usable_basic_nodes[i] = cur;
                }
            }
            pthread_mutex_unlock(node_lock);
            usable_basic_nodes_idx = 0;
        }
        auto basic_node = usable_basic_nodes[usable_basic_nodes_idx++];
        basic_node->com_user_id_ = com_user_id;
        return basic_node.Get();
    }

    void BasicNode::Init() {
        this->user_proc_ref_ = 1;
        this->peer_closed_ = true;
        this->next_.Set(nullptr);
        this->enode_.Set(nullptr);
        this->in_hashtable_ = false;
        this->num_graph_usable_ = 0;
        pthread_spin_init(&this->user_proc_ref_lock_, 1);
        if (this->channel_.Get() == nullptr)
            this->channel_ = memory::Pointer(channel::GetChannel("omni_basic_node_channel_" + 
                std::to_string(memory::process_id) + "_" + std::to_string(memory::thread_id) +
                "_" + std::to_string(++local_create_channel_idx)));
        if (this->packet_pool_.Get() == nullptr)
            this->packet_pool_ = packet::PacketPool::CreatePacketPool("omni_basic_node_packet_pool_" + 
                std::to_string(memory::process_id) + "_" + std::to_string(memory::thread_id) +
                "_" + std::to_string(local_create_channel_idx), 16384);
    }

    void BasicNode::Write(packet::Packet* packet) {
        auto ret = channel_->Write(packet);
        if (ret == 1 && this->enode_.Get()) [[unlikely]] {
            // Flushed
            this->enode_->Write((uint64_t)this);
        }
    }

    void BasicNode::Flush() {
        auto ret = channel_->Flush();
        if (ret == 1 && this->enode_.Get()) [[unlikely]] {
            // Flushed
            this->enode_->Write((uint64_t)this);
        }
    }

    void BasicNode::CleanUp() {
        // Nothing to do
    }

    void ReleaseBasicNode(BasicNode* node) {
        if (num_local_recycled_basic_nodes == kCacheNodes) {
            pthread_mutex_lock(node_lock);
            auto cur = local_recycled_basic_nodes;
            while (cur->GetNext().Get()) cur = cur->GetNext().Get();
            auto new_head = local_recycled_basic_nodes;
            auto new_tail = cur;

            new_tail->GetNext().Set(*recycled_basic_nodes);
            *recycled_basic_nodes = new_head;

            local_recycled_basic_nodes = nullptr;
            num_local_recycled_basic_nodes = 0;
            pthread_mutex_unlock(node_lock);
        }
        ++num_local_recycled_basic_nodes;
        node->CleanUp();
        node->SetNext(memory::Pointer(local_recycled_basic_nodes));
        local_recycled_basic_nodes = node;
    }

    void BasicNode::UpdateInfo(NodeInfo info) {
        if (in_hashtable_)
            throw std::runtime_error("Cannot update info of a node in hashtable");
        this->info_ = info;
    }

    void BasicNode::PutIntoHashtable() {
        if (!in_hashtable_) {
            auto temp_packet = temp_packet_pool->Allocate();
            temp_packet->length_ += sizeof(NodeCommandHeader);
            auto data = temp_packet->data_ + temp_packet->offset_;
            auto header = reinterpret_cast<NodeCommandHeader*>(data);

            header->type = NodeCommandType::kUpdateNodeInfo;
            temp_packet->node_ = this;
            protocol_stack_nodes[0]->Write(temp_packet);
            protocol_stack_nodes[0]->Flush();
        }
        while (!in_hashtable_) usleep(1);
    }

    int BasicNode::OpenRef() {
        int ret = 0;
        pthread_spin_lock(&user_proc_ref_lock_);
        if (user_proc_ref_) {
            user_proc_ref_ ++;
        } else {
            ret = -1;
        }
        pthread_spin_unlock(&user_proc_ref_lock_);
        return ret;
    }

    void BasicNode::CloseRef() {
        bool need_release = false;
        pthread_spin_lock(&user_proc_ref_lock_);
        user_proc_ref_ --;
        if (!user_proc_ref_)
            need_release = true;
        pthread_spin_unlock(&user_proc_ref_lock_);
        if (need_release)
            ClearFromHashtableAndClose();
    }

    void BasicNode::ClearFromHashtableAndClose() {
        auto temp_packet = temp_packet_pool->Allocate();
        temp_packet->length_ += sizeof(NodeCommandHeader);
        auto data = temp_packet->data_ + temp_packet->offset_;
        auto header = reinterpret_cast<NodeCommandHeader*>(data);

        header->type = NodeCommandType::kClearNodeInfo;
        temp_packet->node_ = this;
        protocol_stack_nodes[0]->Write(temp_packet);
    }

    void BasicNode::WriteBottom(packet::Packet* packet) {
        packet->length_ += sizeof(NodeCommandHeader);
        packet->data_ -= sizeof(NodeCommandHeader);
        auto header = reinterpret_cast<NodeCommandHeader*>(packet->data_ + packet->offset_);
        header->type = NodeCommandType::kPacket;
        packet->node_.Set(this);
        protocol_stack_nodes[com_user_id_]->Write(packet);
    }

    packet::Packet* BasicNode::Read() {
        auto ret = (packet::Packet*)channel_->Read();
        return ret;
    }

    void FlushBottom() {
        /// TODO: optimize this
        for (int i = 0; i < kMaxComUser; i ++)
            protocol_stack_nodes[i]->Flush();
    }

    EventNode* CreateEventNode() {
        if (usable_event_nodes_idx == kCacheNodes) {
            pthread_mutex_lock(node_lock);
            for (int i = 0; i < kCacheNodes; i++) {
                if (*recycled_event_nodes) {
                    auto cur = *recycled_event_nodes;
                    (*recycled_event_nodes) = cur->GetNext().Get();
                    cur->Init();
                    usable_event_nodes[i] = cur;
                } else {
                    auto cur = reinterpret_cast<EventNode*>(memory::AllocateNamedShared("", sizeof(EventNode)));
                    cur->Init();
                    usable_event_nodes[i] = cur;
                }
            }
            pthread_mutex_unlock(node_lock);
            usable_event_nodes_idx = 0;
        }
        auto event_node = usable_event_nodes[usable_event_nodes_idx++];
        return event_node.Get();
    }

    uint64_t EventNode::Read() {
        return (uint64_t)channel_->Read();
    }

    void EventNode::Write(uint64_t tnode_id) {
        channel_->Write((void*)tnode_id);
    }

    void EventNode::Init() {
        this->next_.Set(nullptr);
        this->channel_ = memory::Pointer(channel::GetMultiWriterChannel("omni_event_node_channel_" + 
            std::to_string(memory::process_id) + "_" + std::to_string(memory::thread_id) +
            "_" + std::to_string(++local_create_channel_idx)));
    }

    int GetNumNodeUser() {
        return num_node_user;
    }
}