#include "monitor_server.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdexcept>
#include <sstream>
#include <vector>

MonitorServer* MonitorServer::instance_ = nullptr;
bool g_dump_L0 = true;
bool g_dump_L1 = false;
bool g_dump_L2 = false;
unsigned int g_dump_L1_interval = 5;
unsigned int g_dump_L2_interval = 5;
bool g_L1_timer_active = false;
bool g_L2_timer_active = false;
time_t g_L1_start_time = 0;
time_t g_L2_start_time = 0;
pthread_mutex_t g_trace_mutex = PTHREAD_MUTEX_INITIALIZER;  

MonitorServer& MonitorServer::getInstance() {
    std::call_once(init_flag_,
                   []()
                   {
                       instance_ = new MonitorServer();
                   });
    return *instance_;
}

MonitorServer::MonitorServer() {
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::runtime_error("socket creation failed");
    }
}

MonitorServer::~MonitorServer() {
    stop();
}

void MonitorServer::start() {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    unlink(SOCKET_PATH);
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        throw std::runtime_error("socket bind failed");
    }
    
    if (listen(server_fd_, 5) == -1) {
        throw std::runtime_error("socket listen failed");
    }
    
    server_thread_ = std::thread(&MonitorServer::server_thread_func, this);
}

void MonitorServer::stop() {
    if (server_thread_.joinable()) {
        int temp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (temp_fd != -1) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
            connect(temp_fd, (struct sockaddr*)&addr, sizeof(addr));
            close(temp_fd);
        }
        server_thread_.join();
    }
    
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    unlink(SOCKET_PATH);
}

void MonitorServer::server_thread_func() {
    while (true) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            break;
        }
        
        handle_client(client_fd);
    }
}

void MonitorServer::handle_client(int client_fd) {
    char buffer[256];
    ssize_t n = read(client_fd, buffer, sizeof(buffer)-1);
    if (n > 0) {
        buffer[n] = '\0';
        process_command(buffer, [client_fd](const std::string& response) {
            write(client_fd, response.c_str(), response.size());
        });
    }
    close(client_fd);
}

void MonitorServer::process_command(const std::string& cmd, 
                                   const std::function<void(const std::string&)>& send_response) {
    std::istringstream iss(cmd);
    std::vector<std::string> tokens;
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    if (tokens.empty()) {
        send_response("ERROR: Empty command");
        return;
    }
    
    try {
        if (tokens[0] == "set" && tokens.size() >= 2) {
            auto eq_pos = tokens[1].find('=');
            if (eq_pos != std::string::npos) {
                std::string level = tokens[1].substr(0, eq_pos);
                std::string value = tokens[1].substr(eq_pos + 1);
                
                bool enable = (value == "true" || value == "1");
                
                if (level == "L0") g_dump_L0 = enable;
                else if (level == "L1") g_dump_L1 = enable;
                else if (level == "L2") g_dump_L2 = enable;
                else throw std::invalid_argument("Invalid level: " + level);
                
                send_response("OK");
                return;
            }
        }
        else if (tokens[0] == "interval" && tokens.size() >= 2) {
            auto eq_pos = tokens[1].find('=');
            if (eq_pos != std::string::npos) {
                std::string level = tokens[1].substr(0, eq_pos);
                unsigned int interval = std::stoul(tokens[1].substr(eq_pos + 1));
                
                if (level == "L1") g_dump_L1_interval = interval;
                else if (level == "L2") g_dump_L2_interval = interval;
                else throw std::invalid_argument("Invalid level for interval: " + level);
                
                send_response("OK");
                return;
            }
        }
        else if (tokens[0] == "print") {
            std::string level = (tokens.size() >= 2) ? tokens[1] : "all";
            std::ostringstream oss;
            
            auto print_level = [&oss](const std::string& name, bool enabled, unsigned int interval = 0) {
                oss << name << ": " << (enabled ? "enabled" : "disabled");
                if (interval > 0) oss << " (interval: " << interval << ")";
                oss << "\n";
            };
            
            if (level == "all" || level == "L0") print_level("L0", g_dump_L0);
            if (level == "all" || level == "L1") print_level("L1", g_dump_L1, g_dump_L1_interval);
            if (level == "all" || level == "L2") print_level("L2", g_dump_L2, g_dump_L2_interval);
            
            send_response(oss.str());
            return;
        }
    } catch (const std::exception& e) {
        send_response("ERROR: " + std::string(e.what()));
        return;
    }
    
    send_response("ERROR: Invalid command");
}