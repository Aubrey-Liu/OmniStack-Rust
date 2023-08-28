#include <CLI/CLI.hpp>

#include <omnistack/socket/socket.h>
#include <omnistack/common/thread.hpp>
#include <omnistack/memory/memory.h>
#include <omnistack/token/token.h>
#include <omnistack/channel/channel.h>
#include <omnistack/node.h>
#include <omnistack/common/logger.h>

#include <string>
#include <arpa/inet.h>
#include <sys/time.h>

using namespace omnistack;

void InitOmniStack() {
#if defined(OMNIMEM_BACKEND_DPDK)
    memory::InitializeSubsystem(0, true);
#else
    memory::InitializeSubsystem();
#endif
    memory::InitializeSubsystemThread();
    token::InitializeSubsystem();
    channel::InitializeSubsystem();
    node::InitializeSubsystem();
}

static uint64_t GetCurrentTickUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000ll + tv.tv_usec;
}

namespace args {
    static std::string client_server_host = "";
    static std::string server_server_host = "";
    static int server_port;
    static int size;

    bool is_reversed = false;
    bool is_pingpong = false;
    bool is_udp = false;
}

int main(int argc, char **argv) {
    common::Logger::Initialize(std::cerr, "log_test/log");

    CLI::App app{"A simple tool to run performance tests with OmniStack"};
    app.add_option("-c", args::client_server_host, "Run under client mode which used to provide hostname of server")->default_val("");
    auto sever_flag = app.add_option("-s", args::server_server_host, "Run under server mode")->excludes("-c")->default_val("");
    app.add_option("-p", args::server_port, "Port of server")->default_val(31323);
    app.add_flag("--udp", args::is_udp, "UDP test")->default_val(false);
    app.add_flag("--reversed", args::is_reversed, "Reversed test")->default_val(false);
    app.add_flag("--pingpong", args::is_pingpong, "Pingpong test")->default_val(false);
    app.add_option("--size", args::size, "The size per message")->default_val(1430);

    CLI11_PARSE(app, argc, argv);
    OMNI_LOG(common::kInfo) << "Arguments parsed.\n";
    if (args::server_server_host == "" && args::client_server_host == "") {
        OMNI_LOG(common::kError) << "No server host or Server mode provided.\n";
        return 1;
    }
    InitOmniStack();

    if (args::server_server_host != "") {
        /**Write a socket server based on the API in omnistack::socket**/
        auto socket = socket::bsd::socket(AF_INET, args::is_udp ? SOCK_DGRAM : SOCK_STREAM, 0);
        if (socket <= 0) {
            OMNI_LOG(common::kError) << "Failed to create socket.\n";
            return 1;
        }
        OMNI_LOG(common::kInfo) << "Socket created.\n";
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(args::server_port);
        server_addr.sin_addr.s_addr = inet_addr(args::server_server_host.c_str());
        if (socket::bsd::bind(socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            OMNI_LOG(common::kError) << "Failed to bind socket." << std::endl;
            return 1;
        }
        if (args::size < sizeof(uint64_t)) {
            OMNI_LOG(common::kError) << "Size is too small." << std::endl;
            return 1;
        }
        OMNI_LOG(common::kInfo) << "Socket binded.\n";
        if (args::is_udp) {
            if (args::is_pingpong) {
                if (!args::is_reversed) {
                    while (true) {
                        struct sockaddr_in client_addr;
                        socklen_t client_addr_len = sizeof(client_addr);
                        packet::Packet* packet;
                        int recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recvfrom.\n";
                            return 1;
                        }

                        auto new_packet = socket::fast::write_begin(socket);
                        memcpy(new_packet->data_.Get(), packet->data_ + packet->offset_, sizeof(uint64_t));
                        new_packet->length_ = args::size;
                        socket::fast::read_over(socket, packet);

                        int send_len = socket::fast::sendto(socket, new_packet, 0, (struct sockaddr *)&client_addr, client_addr_len);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to sendto.\n";
                            return 1;
                        }
                    }
                } else {
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    packet::Packet* packet;
                    int recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (recv_len < 0) {
                        OMNI_LOG(common::kError) << "Failed to recvfrom." << std::endl;
                        return 1;
                    }
                    socket::fast::read_over(socket, packet);
                    // Client Tell Address

                    uint64_t last_print_tick = GetCurrentTickUs();
                    uint64_t last_sum_tick = 0;
                    int last_sum_tick_count = 0;

                    while (true) {
                        auto new_packet = socket::fast::write_begin(socket);
                        auto cur_tick = GetCurrentTickUs();
                        memcpy(new_packet->data_.Get(), &cur_tick, sizeof(cur_tick));
                        new_packet->length_ = args::size;
                        int send_len = socket::fast::sendto(socket, new_packet, 0, (struct sockaddr *)&client_addr, client_addr_len);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to sendto.\n";
                            return 1;
                        }

                        recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recvfrom.\n";
                            return 1;
                        }
                        uint64_t end_tick = *reinterpret_cast<uint64_t*>(packet->data_ + packet->offset_);
                        last_sum_tick = end_tick - cur_tick;
                        last_sum_tick_count ++;
                        socket::fast::read_over(socket, packet);

                        if (end_tick - last_print_tick > 1000000) { // Print Per 1s
                            OMNI_LOG(common::kInfo) << "Average RTT in last 1 second is " << last_sum_tick / last_sum_tick_count << "us." << std::endl;
                            last_print_tick = end_tick;
                            last_sum_tick = 0;
                            last_sum_tick_count = 0;
                        }
                    }
                }
            } else {
                if (!args::is_reversed) {
                    /** Record the bandwidth of the last second **/
                    uint64_t last_print_tick = GetCurrentTickUs();
                    uint64_t last_sum_bytes = 0;

                    while (true) {
                        packet::Packet* packet;
                        int recv_len = socket::fast::recvfrom(socket, &packet, 0, nullptr, nullptr);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recvfrom.\n";
                            return 1;
                        }
                        last_sum_bytes += recv_len;
                        auto current_tick = GetCurrentTickUs();
                        if (current_tick - last_print_tick > 1000000) { // Print Per 1s
                            OMNI_LOG(common::kInfo) << "Average bandwidth in last 1 second is " << 8.0 * last_sum_bytes / (current_tick - last_print_tick) << "Mbps." << std::endl;
                            last_print_tick = current_tick;
                            last_sum_bytes = 0;
                        }
                        socket::fast::read_over(socket, packet);
                    }
                } else {
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    packet::Packet* packet;
                    int recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (recv_len < 0) {
                        OMNI_LOG(common::kError) << "Failed to recvfrom." << std::endl;
                        return 1;
                    }
                    socket::fast::read_over(socket, packet);

                    while (true) {
                        auto new_packet = socket::fast::write_begin(socket);
                        new_packet->length_ = args::size;
                        int send_len = socket::fast::sendto(socket, new_packet, 0, (struct sockaddr *)&client_addr, client_addr_len);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to sendto.\n";
                            return 1;
                        }
                    }
                }
            }
        } else {
            auto client = socket::bsd::accept(socket, nullptr, nullptr);
            if (args::is_pingpong) {
                if (!args::is_reversed) {
                    while (true) {
                        packet::Packet* packet;
                        int recv_len = socket::fast::read(client, &packet);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recv.\n";
                            return 1;
                        }

                        auto new_packet = socket::fast::write_begin(client);
                        memcpy(new_packet->data_.Get(), packet->data_ + packet->offset_, sizeof(uint64_t));
                        new_packet->length_ = args::size;
                        socket::fast::read_over(client, packet);

                        int send_len = socket::fast::write(client, new_packet);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to send.\n";
                            return 1;
                        }
                    }
                } else {
                    uint64_t last_print_tick = GetCurrentTickUs();
                    uint64_t last_sum_tick = 0;
                    int last_sum_tick_count = 0;

                    /* Send a packet to client with curernt tick and wait the packet back and calc
                        the average latency and print per second. */
                    while (true) {
                        auto new_packet = socket::fast::write_begin(client);
                        auto cur_tick = GetCurrentTickUs();
                        memcpy(new_packet->data_.Get(), &cur_tick, sizeof(cur_tick));
                        new_packet->length_ = args::size;
                        int send_len = socket::fast::write(client, new_packet);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to send.\n";
                            return 1;
                        }

                        packet::Packet* packet;
                        int recv_len = socket::fast::read(client, &packet);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recv.\n";
                            return 1;
                        }
                        uint64_t end_tick = *reinterpret_cast<uint64_t*>(packet->data_ + packet->offset_);
                        last_sum_tick = end_tick - cur_tick;
                        last_sum_tick_count ++;
                        socket::fast::read_over(client, packet);

                        if (end_tick - last_print_tick > 1000000) { // Print Per 1s
                            OMNI_LOG(common::kInfo) << "Average RTT in last 1 second is " << last_sum_tick / last_sum_tick_count << "us." << std::endl;
                            last_print_tick = end_tick;
                            last_sum_tick = 0;
                            last_sum_tick_count = 0;
                        }
                    }
                }
            } else {
                if (!args::is_reversed) {
                    uint64_t last_print_tick = GetCurrentTickUs();
                    uint64_t last_sum_bytes = 0;

                    while (true) {
                        packet::Packet* packet;
                        int recv_len = socket::fast::read(client, &packet);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recv.\n";
                            return 1;
                        }
                        last_sum_bytes += recv_len;
                        auto current_tick = GetCurrentTickUs();
                        if (current_tick - last_print_tick > 1000000) { // Print Per 1s
                            OMNI_LOG(common::kInfo) << "Average bandwidth in last 1 second is " << 8.0 * last_sum_bytes / (current_tick - last_print_tick) << "Mbps." << std::endl;
                            last_print_tick = current_tick;
                            last_sum_bytes = 0;
                        }
                        socket::fast::read_over(client, packet);
                    }
                } else {
                    while (true) {
                        auto new_packet = socket::fast::write_begin(client);
                        new_packet->length_ = args::size;
                        int send_len = socket::fast::write(client, new_packet);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to send.\n";
                            return 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}