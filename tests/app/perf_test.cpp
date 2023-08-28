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

namespace args {
    static std::string client_server_host = "";
    static std::string server_server_host = "";
    static int server_port;

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
            OMNI_LOG(common::kError) << "Failed to bind socket.\n";
            return 1;
        }
        OMNI_LOG(common::kInfo) << "Socket binded.\n";
        if (args::is_udp) {
            if (args::is_pingpong) {
                if (!args::is_reversed) {
                    while (true) {
                        char buffer[1024];
                        struct sockaddr_in client_addr;
                        socklen_t client_addr_len = sizeof(client_addr);
                        packet::Packet* packet;
                        int recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                        if (recv_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to recvfrom.\n";
                            return 1;
                        }
                        auto new_packet = socket::fast::write_begin(socket);
                        memcpy(new_packet->data_.Get(), packet->data_ + packet->offset_, packet->length_ - packet->offset_);
                        new_packet->length_ = packet->length_ - packet->offset_;
                        socket::fast::read_over(socket, packet);

                        int send_len = socket::fast::sendto(socket, new_packet, 0, (struct sockaddr *)&client_addr, client_addr_len);
                        if (send_len < 0) {
                            OMNI_LOG(common::kError) << "Failed to sendto.\n";
                            return 1;
                        }
                    }
                } else {
                    char buffer[1024];
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    packet::Packet* packet;
                    int recv_len = socket::fast::recvfrom(socket, &packet, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (recv_len < 0) {
                        OMNI_LOG(common::kError) << "Failed to recvfrom." << std::endl;
                        return 1;
                    }
                    while (true) {
                        auto new_packet = socket::fast::write_begin(socket);
                        memcpy(new_packet->data_.Get(), packet->data_ + packet->offset_, packet->length_ - packet->offset_);
                        new_packet->length_ = packet->length_ - packet->offset_;
                        socket::fast::read_over(socket, packet);
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
                    }
                }
            }
        }
    }

    return 0;
}