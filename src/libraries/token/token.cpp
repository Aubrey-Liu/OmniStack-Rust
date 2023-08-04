#include <omnistack/token/token.h>
#include <omnistack/memory/memory.h>
#include <filesystem>
#include <thread>
#include <mutex>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>

#if !defined(__APPLE__)
#include <sys/file.h>
#include <condition_variable>
#endif

namespace omnistack::token
{
    static int system_id;
    static int system_id_lock_fd;
    static int control_plane_sock;

    static std::thread* control_plane_thread = nullptr;
    static std::condition_variable cond_control_plane_started;
    static std::mutex control_plane_state_lock;
    static bool control_plane_started = false;
    static volatile bool stop_control_plane = false;

    static void ControlPlane() {

    }

    void StartControlPlane() {
        std::string control_plane_sock_name = "";
        for (system_id = 0; system_id < kMaxControlPlane; system_id ++) {
            auto file_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                std::to_string(system_id) + ".lock";
            system_id_lock_fd = open(file_name.c_str(), O_WRONLY | O_CREAT);
            if (system_id_lock_fd < 0)
                continue;
            if (flock(system_id_lock_fd, LOCK_EX | LOCK_NB)) {
                close(system_id_lock_fd);
                continue;
            }
            control_plane_sock_name = std::filesystem::temp_directory_path().string() + "/omnistack_memory_sock" +
                std::to_string(system_id) + ".socket";
            break;
        }
        if (control_plane_sock_name == "")
            throw std::runtime_error("Too many token control planes");
        
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (control_plane_sock_name.length() >= sizeof(addr.sun_path))
            throw std::runtime_error("Failed to assign sock path to unix domain addr");
        strcpy(addr.sun_path, control_plane_sock_name.c_str());
        control_plane_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (control_plane_sock < 0)
            throw std::runtime_error("Failed to create unix socket");
        std::filesystem::remove(control_plane_sock_name);
        if (bind(control_plane_sock, (struct sockaddr*)&addr,
                    control_plane_sock_name.length() + sizeof(addr.sun_family))) {
            throw std::runtime_error("Failed to bind unix socket " + control_plane_sock_name);
        }
        if (listen(control_plane_sock, 16)) {
            throw std::runtime_error("Failed to listen unix socket");
        }
        {
            auto flags = fcntl(control_plane_sock, F_GETFL);
            if (flags == -1)
                throw std::runtime_error("Faileld to get unix socket flags");
            auto ret = fcntl(control_plane_sock, F_SETFL, (flags | O_NONBLOCK));
            if (ret == -1)
                throw std::runtime_error("Failed to set unix socket flags");
        }

        {
            std::unique_lock _(control_plane_state_lock);
            control_plane_thread = new std::thread(ControlPlane);
            cond_control_plane_started.wait(_, [&](){
                return !control_plane_started;
            });
        }
    }


} // namespace omnistack::token
