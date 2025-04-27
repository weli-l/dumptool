#pragma once
#include <pthread.h>

#include <atomic>
#include <bitset>
#include <boost/process.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "../../include/common/logging.h"
#include "../../include/common/util.h"
#include "../../protos/systrace.pb.h"
#include "../mspti/mspti_tracker.hpp"
#include "library_loader.h"
#include "python/pytorch_tracing_loader.h"

namespace bp = ::boost::process;
namespace systrace
{
using namespace util;
namespace gc_manager
{
class PyGcLibrary;
}

class PyTorchTrace
{
  public:
    PyTorchTrace(const PyTorchTrace &) = delete;
    PyTorchTrace &operator=(const PyTorchTrace &) = delete;
    static PyTorchTrace &getInstance();

    void dumpPyTorchTracing();
    void dumpPyTorchTracing(bool incremental, bool async);
    bool triggerTrace();
    bool shouldDumpGc() const
    {
        return dump_gc_.load(std::memory_order_acquire);
    }
    bool shouldDumpStack() const
    {
        return dump_stack_.load(std::memory_order_acquire);
    }

    void setDumpGc(bool enable)
    {
        dump_gc_.store(enable, std::memory_order_release);
    }
    void setDumpStack(bool enable)
    {
        dump_stack_.store(enable, std::memory_order_release);
    }

    void updateControlFlags()
    {
        std::ifstream file(control_file_);
        if (file)
        {
            std::string line;
            while (std::getline(file, line))
            {
                if (line == "ENABLE_STACK")
                {
                    dump_stack_.store(true);
                    dump_gc_.store(true);
                    break;
                }
            }
        }
        else
        {
            dump_stack_.store(false);
            dump_gc_.store(false);
        }
    }

    const std::string control_file_ = "/tmp/systrace_dump_control";
    static void watchControlFile()
    {
        auto last_mtime = std::filesystem::file_time_type::min();
        while (1)
        {
            try
            {
                const auto current_mtime = std::filesystem::last_write_time(
                    PyTorchTrace::getInstance().control_file_);
                if (current_mtime != last_mtime)
                {
                    PyTorchTrace::getInstance().updateControlFlags();
                    last_mtime = current_mtime;
                    STLOG(INFO)
                        << "Control file updated. Current state: " << "GC="
                        << PyTorchTrace::getInstance().shouldDumpGc()
                        << ", STACK="
                        << PyTorchTrace::getInstance().shouldDumpStack();
                }
            }
            catch (...)
            {
                if (PyTorchTrace::getInstance().shouldDumpStack() ||
                    PyTorchTrace::getInstance().shouldDumpGc())
                {
                    PyTorchTrace::getInstance().setDumpStack(false);
                    PyTorchTrace::getInstance().setDumpGc(false);
                    STLOG(WARNING)
                        << "Control file removed, disabling all dumping";
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

  private:
    inline static PyTorchTrace *instance_ = nullptr;
    inline static std::mutex instance_mutex_;
    inline static std::once_flag init_flag_;

    Pytorch pytorch_trace_;
    std::atomic<bool> has_trigger_trace_{false};
    std::unique_ptr<util::ShmSwitch> switch_;
    std::mutex trace_mutex_;

    std::vector<std::string> pytorch_tracing_functions_;
    pytorch_tracing::PyTorchTracingLibrary *pytorch_tracing_library_;

    PyTorchTrace() = default;
    ~PyTorchTrace() = default;

    static void initSingleton();
    void reset(const std::string &barrier_name);

    std::atomic<bool> dump_gc_{false};
    std::atomic<bool> dump_stack_{false};
    static std::thread file_watcher_;
};

class SysTrace
{
  public:
    SysTrace(const SysTrace &) = delete;
    SysTrace &operator=(const SysTrace &) = delete;

    static SysTrace &getInstance();

  private:
    SysTrace() = default;
    ~SysTrace() { stopWork(); }

    inline static std::atomic<SysTrace *> instance_{nullptr};
    inline static std::mutex instance_mutex_;
    inline static std::once_flag init_flag_;
    std::chrono::steady_clock::time_point sampling_start_;
    bool sampling_active_ = false;

    static void initSingleton();

    std::atomic<bool> should_run_{true};
    std::atomic<uint64_t> loop_count_{0};
    std::thread event_poller_;

    void stopWork() noexcept;
    void doWork();
    void startWork();
};
} // namespace systrace