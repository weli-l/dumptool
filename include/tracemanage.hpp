#ifndef TRACEMANAGE_HPP
#define TRACEMANAGE_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <iostream>
#include "util.hpp"

namespace sys_trace {
using namespace util;
struct DumpConfig {
    std::string path;
    int interval = 60;

    bool IsValid() const {
        return !path.empty() && interval > 0;
    }
};
class TraceManage {
public:
    static TraceManage& getInstance();
    ~TraceManage() {Stop();}
    
    void log(const std::string& message) const;
    void Start(DumpConfig config);
    void Stop();
    void SetDumpConfig(const DumpConfig& config) {
        dump_config_ = config;
    }

    TraceManage(const TraceManage&) = delete;
    TraceManage& operator=(const TraceManage&) = delete;
    TraceManage(TraceManage&&) = delete;
    TraceManage& operator=(TraceManage&&) = delete;


private:
    TraceManage() = default;
    void StartWork();
    void StopWork();
    void dowork();

    static std::unique_ptr<sys_trace::TraceManage> instance;
    static std::once_flag init_flag;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    DumpConfig dump_config_;
};
    
}

#endif // TRACEMANAGE_HPP