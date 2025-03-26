#ifndef TRACEMANAGE_HPP
#define TRACEMANAGE_HPP

#include <memory>
#include <mutex>

class TraceManage {
public:
    static TraceManage& getInstance();
    ~TraceManage() = default;
    
    void log(const std::string& message) const;

    TraceManage(const TraceManage&) = delete;
    TraceManage& operator=(const TraceManage&) = delete;
    TraceManage(TraceManage&&) = delete;
    TraceManage& operator=(TraceManage&&) = delete;


private:
    TraceManage() = default;

    static std::unique_ptr<TraceManage> instance;
    static std::once_flag init_flag;
};

#endif // TRACEMANAGE_HPP