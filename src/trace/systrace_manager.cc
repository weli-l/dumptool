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
  LOG(INFO) << "[PyTorchTrace] Entering getInstance()" << std::endl;
  
  LOG(INFO) << "[PyTorchTrace] Checking singleton initialization status" << std::endl;
  std::call_once(init_flag_, &PyTorchTrace::initSingleton);
  
  LOG(INFO) << "[PyTorchTrace] Returning singleton instance" << std::endl;
  return *instance_;
}

void PyTorchTrace::initSingleton() {
  LOG(INFO) << "[PyTorchTrace] Initializing singleton instance" << std::endl;
  
  instance_ = new PyTorchTrace;
  LOG(INFO) << "[PyTorchTrace] Singleton instance created at address: " << instance_ << std::endl;

  LOG(INFO) << "[PyTorchTrace] Setting rank from GlobalConfig" << std::endl;
  instance_->pytorch_trace_.set_rank(config::GlobalConfig::rank);
  LOG(INFO) << "[PyTorchTrace] Rank set to: " << config::GlobalConfig::rank << std::endl;

  LOG(INFO) << "[PyTorchTrace] Loading PyTorch tracing library" << std::endl;
  instance_->pytorch_tracing_library_ = new pytorch_tracing::PyTorchTracingLibrary("libsysTrace.so");
  LOG(INFO) << "[PyTorchTrace] Tracing library loaded at address: " << instance_->pytorch_tracing_library_ << std::endl;

  LOG(INFO) << "[PyTorchTrace] Registering default tracing functions" << std::endl;
  instance_->pytorch_tracing_functions_ = {
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
  
  LOG(INFO) << "[PyTorchTrace] Checking for additional tracing functions from environment" << std::endl;
  std::string tracing_functions = EnvVarRegistry::GetEnvVar<std::string>("SYSTRACE_HOST_TRACING_FUNC");
  if (tracing_functions != util::EnvVarRegistry::STRING_DEFAULT_VALUE) {
    LOG(INFO) << "[PyTorchTrace] Found additional tracing functions in environment: " << tracing_functions << std::endl;
    std::vector<std::string> funcs = util::split(tracing_functions, ",");
    for (const auto& func : funcs) {
      instance_->pytorch_tracing_functions_.push_back(func);
      LOG(INFO) << "[PyTorchTrace] Added custom tracing function: " << func << std::endl;
    }
  } else {
    LOG(INFO) << "[PyTorchTrace] No additional tracing functions found in environment" << std::endl;
  }

  LOG(INFO) << "[PyTorchTrace] Registering " << instance_->pytorch_tracing_functions_.size() << " tracing functions" << std::endl;
  std::vector<std::string> errors = instance_->pytorch_tracing_library_->Register(instance_->pytorch_tracing_functions_);
  
  // Log registration results
  for (size_t i = 0; i < instance_->pytorch_tracing_functions_.size(); i++) {
    STLOG(INFO) << "[PyTorchTrace] Registration result - Function: " 
               << instance_->pytorch_tracing_functions_[i] 
               << ", Status: " << errors[i] << std::endl;
  }

  //LOG(INFO) << "[PyTorchTrace] Resetting trace state" << std::endl;
  //instance_->reset("init");

  LOG(INFO) << "[PyTorchTrace] Registering atexit cleanup handler" << std::endl;
  std::atexit([] { 
    LOG(INFO) << "[PyTorchTrace] Cleaning up singleton instance" << std::endl;
    delete instance_; 
  });

  LOG(INFO) << "[PyTorchTrace] Singleton initialization complete" << std::endl;
}

// bool PyTorchTrace::triggerTrace() {
//   STLOG(INFO) << "[PyTorchTrace] Entering triggerTrace()" << std::endl ;
  
//   if (switch_->getObj()->reset_flag) {
//     STLOG(INFO) << "[PyTorchTrace] Reset flag is true, preparing to reset tracing" << std::endl ;
    
//     STLOG(INFO) << "[PyTorchTrace] Waiting for all ranks at reset barrier" << std::endl ;
//     // util::InterProcessBarrier(config::GlobalConfig::local_world_size,
//     //                         config::GlobalConfig::local_rank,
//     //                         "reset_trace_barrier");
    
//     STLOG(INFO) << "[PyTorchTrace] Clearing reset flag" << std::endl ;
//     switch_->getObj()->reset_flag = false;
    
//     STLOG(INFO) << "[PyTorchTrace] Resetting trace state" << std::endl ;
//     reset("reset");
    
//     STLOG(INFO) << "[PyTorchTrace] Trace reset complete, returning false" << std::endl ;
//     return false;
//   }
  
//   if (has_trigger_trace_) {
//     STLOG(INFO) << "[PyTorchTrace] Trace already triggered, returning true" << std::endl ;
//     return true;
//   }
  
//   if (switch_->getObj()->start_dump == 0) {
//     STLOG(INFO) << "[PyTorchTrace] start_dump is 0, tracing not enabled, returning false" << std::endl ;
//     return false;
//   }
  
//   auto now = std::chrono::system_clock::now();
//   time_t now_time_t = std::chrono::system_clock::to_time_t(now);
//   int64_t now_timestamp = static_cast<std::int64_t>(now_time_t);
  
//   STLOG(INFO) << "[PyTorchTrace] Current timestamp: " << now_timestamp 
//              << ", Trigger timestamp: " << switch_->getObj()->timestamp;
  
//   if (now_timestamp >= switch_->getObj()->timestamp) {
//     has_trigger_trace_ = true;
//     pytorch_tracing_library_->SwitchTracing(1);
//     STLOG(INFO) << "[PyTorchTrace] Triggering trace after timestamp " 
//                << switch_->getObj()->timestamp;
//     return true;
//   }
  
//   STLOG(INFO) << "[PyTorchTrace] Not yet time to trigger trace" << std::endl ;
//   return false;
// }

bool PyTorchTrace::triggerTrace() {
  return true;
}

void PyTorchTrace::reset(const std::string& barrier_name) {
  STLOG(INFO) << "[PyTorchTrace] Entering reset() with barrier name: " << barrier_name;
  
  has_trigger_trace_ = false;
  pytorch_tracing_library_->SwitchTracing(0);
  
  STLOG(INFO) << "[PyTorchTrace] Waiting at barrier: " << barrier_name;
  // util::InterProcessBarrier(config::GlobalConfig::local_world_size,
  //                         config::GlobalConfig::local_rank, barrier_name);
  
  STLOG(INFO) << "[PyTorchTrace] Reset complete. Current start_dump value: " 
             << switch_->getObj()->start_dump;
}

void PyTorchTrace::dumpPyTorchTracing() {
  STLOG(INFO) << "[PyTorchTrace] Entering dumpPyTorchTracing()" << std::endl ;
  
  // Disable Python-side tracing to ensure data consistency
  STLOG(INFO) << "[PyTorchTrace] Disabling Python tracing" << std::endl ;
  pytorch_tracing_library_->SwitchTracing(0);

  const std::string& dump_path = switch_->getObj()->dump_path;
  STLOG(INFO) << "[PyTorchTrace] Preparing to dump to path: " << dump_path;

  // Setup cleanup guard
  util::ScopeGuard guard([this]() {
    STLOG(INFO) << "[PyTorchTrace] Executing post-dump cleanup" << std::endl ;
    reset("after_dump");
    pytorch_trace_.mutable_pytorch_stages()->Clear();
    STLOG(INFO) << "[PyTorchTrace] Cleanup complete" << std::endl ;
  });

  // Ensure dump directory exists
  if (util::ensureDirExists(dump_path)) {
    STLOG(ERROR) << "[PyTorchTrace] Failed to create directory for timeline.meta" << std::endl ;
    return;
  }
  STLOG(INFO) << "[PyTorchTrace] Directory verified/created: " << dump_path;

  // Process tracing data for each registered function
  STLOG(INFO) << "[PyTorchTrace] Processing tracing data for " 
             << pytorch_tracing_functions_.size() << " functions" << std::endl ;
  
  for (size_t name_index = 0; name_index < pytorch_tracing_functions_.size();
       name_index++) {
    std::vector<PyTorchTracingDataArray*> holders;
    const std::string& name = pytorch_tracing_functions_[name_index];
    
    STLOG(INFO) << "[PyTorchTrace] Processing function: " << name 
               << " (index: " << name_index << ")" << std::endl ;

    // Get partial tracing data
    PyTorchTracingDataArray* tracing_data =
        pytorch_tracing_library_->GetPartialTracingData(name_index);
    if (tracing_data) {
      STLOG(INFO) << "[PyTorchTrace] Retrieved partial tracing data for " << name;
      holders.push_back(tracing_data);
    }

    // Get all full tracing data from queue
    int full_data_count = 0;
    while (true) {
      PyTorchTracingDataArray* tracing_data =
          pytorch_tracing_library_->GetFullTracingData(name_index);
      if (!tracing_data) break;
      
      full_data_count++;
      holders.push_back(tracing_data);
    }
    STLOG(INFO) << "[PyTorchTrace] Retrieved " << full_data_count 
               << " full tracing data sets for " << name;

    if (holders.empty()) {
      STLOG(INFO) << "[PyTorchTrace] No tracing data available for " << name;
      continue;
    }

    // Process all collected tracing data
    STLOG(INFO) << "[PyTorchTrace] Processing " << holders.size() 
               << " data holders for " << name;
    
    int inner_index = 0;
    for (auto each_tracing_data : holders) {
      STLOG(INFO) << "[PyTorchTrace] Processing holder " << inner_index++ 
                 << " with " << each_tracing_data->cur << " entries" << std::endl ;
      
      for (uint32_t i = 0; i < each_tracing_data->cur; i++) {
        if (each_tracing_data->data[i].start == 0) {
          STLOG(INFO) << "[PyTorchTrace] Skipping entry with start=0" << std::endl ;
          continue;
        }
        
        auto trace = pytorch_trace_.add_pytorch_stages();
        trace->set_start_us(each_tracing_data->data[i].start);
        trace->set_end_us(each_tracing_data->data[i].end);
        trace->set_stage_id(each_tracing_data->data[i].count);
        trace->set_stage_type(name);
        
        // Process stack frames
        std::string combined_stack;
        for (uint32_t j = 0; j < each_tracing_data->data[i].stack_depth; j++) {
            if (!combined_stack.empty()) {
              combined_stack += "|";
            }
            combined_stack += each_tracing_data->data[i].stack[j];
        }
        trace->add_stack_frames()->assign(combined_stack);
        
        STLOG(INFO) << "[PyTorchTrace] Added trace entry: " 
                    << name << " [" << each_tracing_data->data[i].start 
                    << " - " << each_tracing_data->data[i].end << "]" << std::endl ;

        if (each_tracing_data->data[i].type == PAYLOAD_GC) {
          hook::GcDebugData* gc_debug = trace->mutable_gc_debug();
          gc_debug->set_collected(
              each_tracing_data->data[i].payload.gc_debug[0]);
          gc_debug->set_uncollectable(
              each_tracing_data->data[i].payload.gc_debug[1]);
          
          STLOG(INFO) << "[PyTorchTrace] Added GC debug info: collected=" 
                      << each_tracing_data->data[i].payload.gc_debug[0]
                      << ", uncollectable=" 
                      << each_tracing_data->data[i].payload.gc_debug[1];
        }
      }
    }

    // Return tracing data to pool
    STLOG(INFO) << "[PyTorchTrace] Returning " << holders.size() 
               << " data holders to pool" << std::endl ;
    for (auto each_tracing_data : holders) {
      pytorch_tracing_library_->ReturnTracingData(each_tracing_data,
                                             PY_TRACING_EMPTY_POOL, name_index);
    }
  }

  // Generate unique timeline file path
  std::ostringstream oss;
  oss << dump_path << "/" << util::getUniqueFileNameByCluster(".timeline");
  std::string file_path(oss.str());
  STLOG(INFO) << "[PyTorchTrace] Final timeline file path: " << file_path;

  // Serialize and write the protobuf data
  STLOG(INFO) << "[PyTorchTrace] Serializing timeline data" << std::endl ;
  std::ofstream file(file_path, std::ios::binary | std::ios::out);
  if (!file) {
    STLOG(FATAL) << "[PyTorchTrace] Failed to open file for writing: " << file_path;
    return;
  }
  
  std::string binary_message;
  if (!pytorch_trace_.SerializeToString(&binary_message)) {
    STLOG(ERROR) << "[PyTorchTrace] Failed to serialize timeline data" << std::endl ;
    return;
  }
  
  file << binary_message;
  STLOG(INFO) << "[PyTorchTrace] Successfully dumped timeline for rank " 
             << config::GlobalConfig::rank << " to " << file_path;
}

void SysTrace::initSingleton() {
  STLOG(INFO) << "[SysTrace] Initializing SysTrace singleton" << std::endl ;
  
  // Register environment variables first
  STLOG(INFO) << "[SysTrace] Registering environment variables" << std::endl ;
  util::REGISTER_ENV();
  
  STLOG(INFO) << "[SysTrace] Creating SysTrace instance" << std::endl ;
  instance_ = new SysTrace;

  STLOG(INFO) << "[SysTrace] Starting SysTrace worker" << std::endl ;
  instance_->startWork();

  STLOG(INFO) << "[SysTrace] Registering atexit cleanup handler" << std::endl ;
  std::atexit([] { 
    STLOG(INFO) << "[SysTrace] Cleaning up SysTrace instance" << std::endl ;
    delete instance_; 
  });
  
  STLOG(INFO) << "[SysTrace] Singleton initialization complete" << std::endl ;
}

void SysTrace::stopWork() noexcept {
  STLOG(INFO) << "[SysTrace] Entering stopWork()" << std::endl ;
  
  if (!config::GlobalConfig::enable) {
    STLOG(INFO) << "[SysTrace] Tracing not enabled, skipping stop" << std::endl ;
    return;
  }

  STLOG(INFO) << "[SysTrace] Setting should_run flag to false" << std::endl ;
  should_run_.store(false);
  
  STLOG(INFO) << "[SysTrace] Stopping poller thread..." << std::endl ;
  if (event_poller_.joinable()) {
    STLOG(INFO) << "[SysTrace] Joining poller thread" << std::endl ;
    event_poller_.join();
    STLOG(INFO) << "[SysTrace] Poller thread stopped" << std::endl ;
  }
  
  STLOG(INFO) << "[SysTrace] Stopping Metrics manager" << std::endl ;
  STLOG(INFO) << "[SysTrace] Thread stopped, exiting process" << std::endl ;
}

void SysTrace::doWork() {
  STLOG(INFO) << "[SysTrace] Worker thread started" << std::endl ;
  
  while (should_run_.load()) {
    STLOG(INFO) << "[SysTrace] Worker loop iteration: " << loop_count_ << std::endl;
    
    // if (loop_count_ >= 1000) {  // 1000 is for warmup
      STLOG(INFO) << "[SysTrace] Warmup complete, checking trace trigger" << std::endl ;
      
      if (PyTorchTrace::getInstance().triggerTrace()) {
        STLOG(INFO) << "[SysTrace] Trace triggered, dumping tracing data" << std::endl ;
        PyTorchTrace::getInstance().dumpPyTorchTracing();
      }
    // }
    // loop_count_++;
    
    // Small delay to prevent busy waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  STLOG(INFO) << "[SysTrace] Worker thread exiting" << std::endl ;
}

SysTrace& SysTrace::getInstance() {
  STLOG(INFO) << "[SysTrace] Getting SysTrace instance" << std::endl ;
  std::call_once(init_flag_, &SysTrace::initSingleton);
  return *instance_;
}

void SysTrace::startWork() {
  STLOG(INFO) << "[SysTrace] Starting SysTrace work" << std::endl ;
  
  // Global config setup
  STLOG(INFO) << "[SysTrace] Setting up global configuration" << std::endl ;
  config::setUpConfig();
  
  if (!config::GlobalConfig::enable) {
    STLOG(INFO) << "[SysTrace] Tracing not enabled in configuration" << std::endl ;
    return;
  }
  
  int local_rank = config::GlobalConfig::local_rank;
  STLOG(INFO) << "[SysTrace] Setting up logging for local rank: " << local_rank;
  setLoggingPath();

  // Initialize PyTorch trace
  STLOG(INFO) << "[SysTrace] Initializing PyTorchTrace" << std::endl ;
  PyTorchTrace::getInstance();
  
  // Wait for all ranks to start
  STLOG(INFO) << "[SysTrace] Waiting at start work barrier" << std::endl ;
  // util::InterProcessBarrier(config::GlobalConfig::local_world_size,
  //                         local_rank, "start_work_barrier");
  
  STLOG(INFO) << "[SysTrace] Setting should_run flag to true" << std::endl ;
  should_run_.store(true);

#ifdef _GNU_SOURCE
  // Start poller thread with custom name
  STLOG(INFO) << "[SysTrace] Starting event poller thread" << std::endl ;
  event_poller_ = std::thread(&SysTrace::doWork, this);
  auto handle = event_poller_.native_handle();
  pthread_setname_np(handle, "systrace_poller");
  STLOG(INFO) << "[SysTrace] Event poller thread started with name 'systrace_poller'" << std::endl ;
#endif

  STLOG(INFO) << "[SysTrace] Work started successfully" << std::endl ;
}
} // namespace systrace