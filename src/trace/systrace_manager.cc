#include <signal.h>
#include <unistd.h>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/process/extend.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#include "systrace_manager.h"
#include "../../include/common/logging.h"

namespace bip = boost::interprocess;
namespace systrace {

PyTorchTrace& PyTorchTrace::getInstance() {
  STLOG(INFO) << "[PyTorchTrace] Entering getInstance()";
  
  PyTorchTrace* instance = instance_.load(std::memory_order_acquire);
  if (!instance) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    instance = instance_.load(std::memory_order_relaxed);
    if (!instance) {
      STLOG(INFO) << "[PyTorchTrace] Initializing singleton instance";
      instance = new PyTorchTrace();
      instance_.store(instance, std::memory_order_release);
      
      // Initialize rank and library
      instance->pytorch_trace_.set_rank(config::GlobalConfig::rank);
      STLOG(INFO) << "[PyTorchTrace] Rank set to: " << config::GlobalConfig::rank;
      
      instance->pytorch_tracing_library_ = new pytorch_tracing::PyTorchTracingLibrary("libsysTrace.so");
      STLOG(INFO) << "[PyTorchTrace] Tracing library loaded";
      
      // Register tracing functions
      instance->pytorch_tracing_functions_ = {
        "GC",
        "torch.utils.data.dataloader@_BaseDataLoaderIter@__next__",
        "torch@cuda@synchronize",
        "torch.cuda@Event@synchronize",
        "torch.cuda@Event@wait",
        "torch.cuda@Stream@synchronize",
        "torch.cuda@Stream@wait_event",
        "torch.cuda@Stream@wait_stream",
        "torch@autograd@backward",
        "torch@autograd@grad",
        "megatron.core.pipeline_parallel@schedules@forward_step",
        "megatron.core.pipeline_parallel@schedules@backward_step"
      };
      
      std::string tracing_functions = EnvVarRegistry::GetEnvVar<std::string>("SYSTRACE_HOST_TRACING_FUNC");
      if (tracing_functions != util::EnvVarRegistry::STRING_DEFAULT_VALUE) {
        std::vector<std::string> funcs = util::split(tracing_functions, ",");
        for (const auto& func : funcs) {
          instance->pytorch_tracing_functions_.push_back(func);
        }
        STLOG(INFO) << "[PyTorchTrace] Added " << funcs.size() << " custom tracing functions";
      }
      
      std::vector<std::string> errors = instance->pytorch_tracing_library_->Register(instance->pytorch_tracing_functions_);
      for (size_t i = 0; i < instance->pytorch_tracing_functions_.size(); i++) {
        STLOG(INFO) << "[PyTorchTrace] Registration - " 
                   << instance->pytorch_tracing_functions_[i] 
                   << ": " << errors[i];
      }
      
      std::atexit([] {
        STLOG(INFO) << "[PyTorchTrace] Cleaning up singleton instance";
        delete instance_.load();
      });
    }
  }
  
  return *instance;
}

bool PyTorchTrace::triggerTrace() {
  std::lock_guard<std::mutex> lock(trace_mutex_);
  STLOG(INFO) << "[PyTorchTrace] Entering triggerTrace()";
  
  if (!switch_) {
    STLOG(INFO) << "[PyTorchTrace] Switch not initialized, skipping trace";
    return false;
  }

  if (switch_->getObj()->reset_flag) {
    STLOG(INFO) << "[PyTorchTrace] Reset flag detected, resetting trace state";
    util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                            config::GlobalConfig::local_rank,
                            "reset_trace_barrier");
    switch_->getObj()->reset_flag = false;
    reset("reset");
    return false;
  }
  
  if (has_trigger_trace_.load()) {
    STLOG(INFO) << "[PyTorchTrace] Trace already triggered";
    return true;
  }
  
  if (switch_->getObj()->start_dump == 0) {
    STLOG(INFO) << "[PyTorchTrace] Tracing not enabled";
    return false;
  }
  
  auto now = std::chrono::system_clock::now();
  time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  int64_t now_timestamp = static_cast<std::int64_t>(now_time_t);
  
  if (now_timestamp >= switch_->getObj()->timestamp) {
    has_trigger_trace_.store(true);
    pytorch_tracing_library_->SwitchTracing(1);
    STLOG(INFO) << "[PyTorchTrace] Trace triggered at timestamp " << now_timestamp;
    return true;
  }
  
  STLOG(INFO) << "[PyTorchTrace] Waiting for trace trigger time";
  return false;
}

void PyTorchTrace::reset(const std::string& barrier_name) {
  std::lock_guard<std::mutex> lock(trace_mutex_);
  STLOG(INFO) << "[PyTorchTrace] Resetting trace state at barrier: " << barrier_name;
  
  has_trigger_trace_.store(false);
  pytorch_tracing_library_->SwitchTracing(0);
  
  util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                          config::GlobalConfig::local_rank, 
                          barrier_name);
  
  STLOG(INFO) << "[PyTorchTrace] Reset complete";
}

void PyTorchTrace::dumpPyTorchTracing() {
  std::lock_guard<std::mutex> lock(trace_mutex_);
  STLOG(INFO) << "[PyTorchTrace] Starting dumpPyTorchTracing";
  
  pytorch_tracing_library_->SwitchTracing(0);

  if (!switch_ || !switch_->getObj()) {
    STLOG(ERROR) << "[PyTorchTrace] Invalid switch state, cannot dump";
    return;
  }
  
  const std::string& dump_path = switch_->getObj()->dump_path;
  STLOG(INFO) << "[PyTorchTrace] Dump path: " << dump_path;

  util::ScopeGuard guard([this]() {
    reset("after_dump");
    pytorch_trace_.mutable_pytorch_stages()->Clear();
    STLOG(INFO) << "[PyTorchTrace] Post-dump cleanup complete";
  });

  if (util::ensureDirExists(dump_path)) {
    STLOG(ERROR) << "[PyTorchTrace] Failed to create dump directory";
    return;
  }

  for (size_t name_index = 0; name_index < pytorch_tracing_functions_.size(); name_index++) {
    std::vector<PyTorchTracingDataArray*> holders;
    const std::string& name = pytorch_tracing_functions_[name_index];
    
    PyTorchTracingDataArray* tracing_data = pytorch_tracing_library_->GetPartialTracingData(name_index);
    if (tracing_data) holders.push_back(tracing_data);

    int full_data_count = 0;
    while (true) {
      PyTorchTracingDataArray* tracing_data = pytorch_tracing_library_->GetFullTracingData(name_index);
      if (!tracing_data) break;
      full_data_count++;
      holders.push_back(tracing_data);
    }
    
    STLOG(INFO) << "[PyTorchTrace] Processing " << holders.size() 
               << " data holders for " << name;

    for (auto each_tracing_data : holders) {
      for (uint32_t i = 0; i < each_tracing_data->cur; i++) {
        if (each_tracing_data->data[i].start == 0) continue;
        
        auto trace = pytorch_trace_.add_pytorch_stages();
        trace->set_start_us(each_tracing_data->data[i].start);
        trace->set_end_us(each_tracing_data->data[i].end);
        trace->set_stage_id(each_tracing_data->data[i].count);
        trace->set_stage_type(name);
        
        std::string combined_stack;
        for (uint32_t j = 0; j < each_tracing_data->data[i].stack_depth; j++) {
          if (!combined_stack.empty()) combined_stack += "|";
          combined_stack += each_tracing_data->data[i].stack[j];
        }
        trace->add_stack_frames()->assign(combined_stack);
        
        if (each_tracing_data->data[i].type == PAYLOAD_GC) {
          hook::GcDebugData* gc_debug = trace->mutable_gc_debug();
          gc_debug->set_collected(each_tracing_data->data[i].payload.gc_debug[0]);
          gc_debug->set_uncollectable(each_tracing_data->data[i].payload.gc_debug[1]);
        }
      }
    }

    for (auto each_tracing_data : holders) {
      pytorch_tracing_library_->ReturnTracingData(each_tracing_data,
                                               PY_TRACING_EMPTY_POOL, 
                                               name_index);
    }
  }

  std::string file_path = dump_path + "/" + util::getUniqueFileNameByCluster(".timeline");
  STLOG(INFO) << "[PyTorchTrace] Writing timeline to: " << file_path;

  std::ofstream file(file_path, std::ios::binary | std::ios::out);
  if (!file) {
    STLOG(ERROR) << "[PyTorchTrace] Failed to open timeline file";
    return;
  }
  
  std::string binary_message;
  if (!pytorch_trace_.SerializeToString(&binary_message)) {
    STLOG(ERROR) << "[PyTorchTrace] Failed to serialize timeline";
    return;
  }
  
  file << binary_message;
  STLOG(INFO) << "[PyTorchTrace] Timeline dump complete";
}

SysTrace& SysTrace::getInstance() {
  STLOG(INFO) << "[SysTrace] Getting instance";
  
  SysTrace* instance = instance_.load(std::memory_order_acquire);
  if (!instance) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    instance = instance_.load(std::memory_order_relaxed);
    if (!instance) {
      STLOG(INFO) << "[SysTrace] Initializing singleton instance";
      instance = new SysTrace();
      instance_.store(instance, std::memory_order_release);
      
      STLOG(INFO) << "[SysTrace] Starting work thread";
      instance->startWork();
      
      std::atexit([] {
        STLOG(INFO) << "[SysTrace] Cleaning up instance";
        delete instance_.load();
      });
    }
  }
  
  return *instance;
}

void SysTrace::stopWork() noexcept {
  STLOG(INFO) << "[SysTrace] Stopping work";
  
  if (!config::GlobalConfig::enable) {
    STLOG(INFO) << "[SysTrace] Tracing not enabled";
    return;
  }

  should_run_.store(false);
  
  if (event_poller_.joinable()) {
    STLOG(INFO) << "[SysTrace] Joining poller thread";
    event_poller_.join();
    STLOG(INFO) << "[SysTrace] Poller thread stopped";
  }
  
  STLOG(INFO) << "[SysTrace] Work stopped";
}

void SysTrace::doWork() {
  STLOG(INFO) << "[SysTrace] Worker thread started";
  
  while (should_run_.load()) {
    STLOG(INFO) << "[SysTrace] Worker loop iteration: " << loop_count_.load();
    
    if (PyTorchTrace::getInstance().triggerTrace()) {
      STLOG(INFO) << "[SysTrace] Trace triggered, dumping data";
      PyTorchTrace::getInstance().dumpPyTorchTracing();
    }
    
    loop_count_.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  STLOG(INFO) << "[SysTrace] Worker thread exiting";
}

void SysTrace::startWork() {
  STLOG(INFO) << "[SysTrace] Starting work";
  
  config::setUpConfig();
  
  if (!config::GlobalConfig::enable) {
    STLOG(INFO) << "[SysTrace] Tracing not enabled";
    return;
  }
  
  int local_rank = config::GlobalConfig::local_rank;
  STLOG(INFO) << "[SysTrace] Local rank: " << local_rank;
  setLoggingPath();

  // Initialize PyTorch trace
  PyTorchTrace::getInstance();
  
  util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                          local_rank, 
                          "start_work_barrier");
  
  should_run_.store(true);

#ifdef _GNU_SOURCE
  STLOG(INFO) << "[SysTrace] Starting poller thread";
  event_poller_ = std::thread(&SysTrace::doWork, this);
  pthread_setname_np(event_poller_.native_handle(), "systrace_poller");
#endif

  STLOG(INFO) << "[SysTrace] Work started";
}
} // namespace systrace