#ifndef TRACEMANAGE_HPP
#define TRACEMANAGE_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <iostream>

class TraceManage {
public:
    static TraceManage& getInstance();
    ~TraceManage() {Stop();}
    
    void log(const std::string& message) const;
    void Start();
    void Stop();

    TraceManage(const TraceManage&) = delete;
    TraceManage& operator=(const TraceManage&) = delete;
    TraceManage(TraceManage&&) = delete;
    TraceManage& operator=(TraceManage&&) = delete;


private:
    TraceManage() = default;
    void StartWork();
    void StopWork();
    void dowork();

    static std::unique_ptr<TraceManage> instance;
    static std::once_flag init_flag;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

#endif // TRACEMANAGE_HPP