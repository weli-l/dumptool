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

#include "../../include/common/util.h"
#include "library_loader.h"
#include "python/pytorch_tracing_loader.h"
#include "../../protos/systrace.pb.h"

namespace bp = ::boost::process;
namespace systrace {
using namespace util;
using namespace hook;
namespace gc_manager {
class PyGcLibrary;
}

class PyTorchTrace {
 public:
  PyTorchTrace(const PyTorchTrace&) = delete;
  PyTorchTrace& operator=(const PyTorchTrace&) = delete;
  static PyTorchTrace& getInstance();

  void dumpPyTorchTracing();
  void dumpPyTorchTracing(bool incremental, bool async);
  bool triggerTrace();

 private:
  inline static PyTorchTrace* instance_ = nullptr;
  inline static std::mutex instance_mutex_;
  inline static std::once_flag init_flag_;

  hook::Pytorch pytorch_trace_;
  std::atomic<bool> has_trigger_trace_{false};
  std::unique_ptr<util::ShmSwitch> switch_;
  std::mutex trace_mutex_;

  std::vector<std::string> pytorch_tracing_functions_;
  pytorch_tracing::PyTorchTracingLibrary* pytorch_tracing_library_;

  PyTorchTrace() = default;
  ~PyTorchTrace() = default;

  static void initSingleton();
  void reset(const std::string& barrier_name);
};

class SysTrace {
 public:
  SysTrace(const SysTrace&) = delete;
  SysTrace& operator=(const SysTrace&) = delete;

  static SysTrace& getInstance();
  
 private:
  SysTrace() = default;
  ~SysTrace() { stopWork(); }

  inline static std::atomic<SysTrace*> instance_{nullptr};
  inline static std::mutex instance_mutex_;
  inline static std::once_flag init_flag_;
  
  static void initSingleton();
  
  std::atomic<bool> should_run_{true};
  std::atomic<uint64_t> loop_count_{0};
  std::thread event_poller_;
  
  void stopWork() noexcept;
  void doWork();
  void startWork();
};
}  // namespace systrace