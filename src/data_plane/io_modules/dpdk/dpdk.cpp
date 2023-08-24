#include <omnistack/io/io_adapter.hpp>

namespace omnistack::io_module::dpdk {
    constexpr char kName[] = "Dpdk";

    class DpdkAdapter : public io::IoAdapter<DpdkAdapter, kName> {
    public:
        DpdkAdapter() {}
        virtual ~DpdkAdapter() {}

        virtual void InitializeDriver() override;
            
        virtual int AcqurieNumAdapters() override;

        virtual void InitializeAdapter(int port_id) override;

        virtual void InitializeQueue(int queue_id, packet::PacketPool* packet_pool) override;

        virtual void SendPacket(int queue_id, packet::Packet* packet) override;
        /** Periodcally called **/
        virtual void FlushSendPacket(int queue_id) override;

        virtual void Start() override;

        virtual packet::Packet* RecvPackets(int queue_id) override;

        virtual void RedirectFlow(packet::Packet* packet)  override;
    private:
        packet::PacketPool* packet_pool_;
    };
}