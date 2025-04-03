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
  std::call_once(init_flag_, &PyTorchTrace::initSingleton);
  return *instance_;
  // std::cout <<"sb" << std::endl;
  // static PyTorchTrace instance;  // This will be initialized on first call
  // return instance;
}

void PyTorchTrace::initSingleton() {
  instance_ = new PyTorchTrace;

  instance_->pytorch_trace_.set_rank(config::GlobalConfig::rank);

  instance_->switch_ = std::make_unique<util::ShmSwitch>(
      config::GlobalConfig::local_world_size + 1,
      config::GlobalConfig::local_rank, false);

  instance_->pytorch_tracing_library_ =
      new pytorch_tracing::PyTorchTracingLibrary("libpytorch_tracing.so");
  instance_->pytorch_tracing_functions_.push_back("GC");
  instance_->pytorch_tracing_functions_.push_back(
      "torch.utils.data.dataloader@_BaseDataLoaderIter@__next__");
  instance_->pytorch_tracing_functions_.push_back("torch@cuda@synchronize");
  instance_->pytorch_tracing_functions_.push_back("torch.cuda@Event@synchronize");
  instance_->pytorch_tracing_functions_.push_back("torch.cuda@Event@wait");
  instance_->pytorch_tracing_functions_.push_back("torch.cuda@Stream@synchronize");
  instance_->pytorch_tracing_functions_.push_back("torch.cuda@Stream@wait_event");
  instance_->pytorch_tracing_functions_.push_back("torch.cuda@Stream@wait_stream");
  instance_->pytorch_tracing_functions_.push_back("torch@autograd@backward");
  instance_->pytorch_tracing_functions_.push_back("torch@autograd@grad");
  instance_->pytorch_tracing_functions_.push_back(
      "megatron.core.pipeline_parallel@schedules@forward_step");
  instance_->pytorch_tracing_functions_.push_back(
      "megatron.core.pipeline_parallel@schedules@backward_step");
  std::string tracing_functions =
      EnvVarRegistry::GetEnvVar<std::string>("SYSTRACE_HOST_TRACING_FUNC");
  if (tracing_functions != util::EnvVarRegistry::STRING_DEFAULT_VALUE) {
    std::vector<std::string> funcs = util::split(tracing_functions, ",");
    for (const auto& func : funcs)
      instance_->pytorch_tracing_functions_.push_back(func);
  }
  std::vector<std::string> errors = instance_->pytorch_tracing_library_->Register(
      instance_->pytorch_tracing_functions_);
  for (size_t i = 0; i < instance_->pytorch_tracing_functions_.size(); i++) {
    STLOG(INFO) << "Resiter host function "
               << instance_->pytorch_tracing_functions_[i] << ",status "
               << errors[i];
  }
  instance_->reset("init");
  std::atexit([] { delete instance_; });
}

bool PyTorchTrace::triggerTrace() {
  if (switch_->getObj()->reset_flag) {
    // ensure all ranks into reseting and set reset_flag to false
    util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                              config::GlobalConfig::local_rank,
                              "reset_trace_barrier");
    switch_->getObj()->reset_flag = false;
    STLOG(INFO) << "Reset all status";
    reset("reset");
    return false;
  }
  if (has_trigger_trace_) return true;
  if (switch_->getObj()->start_dump == 0) return false;
  auto now = std::chrono::system_clock::now();
  time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  int64_t now_timestamp = static_cast<std::int64_t>(now_time_t);
  if (now_timestamp >= switch_->getObj()->timestamp) {
    has_trigger_trace_ = true;
    pytorch_tracing_library_->SwitchTracing(1);
    STLOG(INFO) << "Trigger trace after " << switch_->getObj()->timestamp;
    return true;
  }
  return false;
}

void PyTorchTrace::reset(const std::string& barrier_name) {
  has_trigger_trace_ = false;
  pytorch_tracing_library_->SwitchTracing(0);
  // make sure all training ranks into reset
  util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                            config::GlobalConfig::local_rank, barrier_name);
  switch_->getObj()->start_dump = 0;
  STLOG(INFO) << barrier_name << " reset start_dump to "
             << switch_->getObj()->start_dump;
}

void PyTorchTrace::dumpPyTorchTracing() {

  // 关闭 Python 侧的 tracing 记录，确保数据完整。
  pytorch_tracing_library_->SwitchTracing(0);

  // dump_path 指定 .timeline 文件的存储路径。
  const std::string& dump_path = switch_->getObj()->dump_path;

  util::ScopeGuard guard([this]() {
    reset("after_dump");
    pytorch_trace_.mutable_pytorch_stages()->Clear();
  });

  // 确保 dump 目录存在，否则报错。
  if (util::ensureDirExists(dump_path)) {
    STLOG(ERROR) << "Could not create dir for timeline.meta";
    return;
  }

    // fetch all host tracing data
  // 1. get current trace data
  // 2. get trace data from ready queue
  // 3. dump to protobuf
  // 4. return to pool of trace data
  for (size_t name_index = 0; name_index < pytorch_tracing_functions_.size();
       name_index++) {
    std::vector<PyTorchTracingDataArray*> holders;
    const std::string& name = pytorch_tracing_functions_[name_index];
    PyTorchTracingDataArray* tracing_data =
        pytorch_tracing_library_->GetPartialTracingData(name_index);
    if (tracing_data) holders.push_back(tracing_data);
    while (1) {
      PyTorchTracingDataArray* tracing_data =
          pytorch_tracing_library_->GetFullTracingData(name_index);
      if (!tracing_data) break;
      holders.push_back(tracing_data);
    }
    if (!holders.size()) continue;
    int inner_index = 0;
    for (auto each_tracing_data : holders) {
      for (uint32_t i = 0; i < each_tracing_data->cur; i++) {
        if (each_tracing_data->data[i].start == 0)
          continue;
        auto trace = pytorch_trace_.add_pytorch_stages();
        trace->set_start_us(each_tracing_data->data[i].start);
        trace->set_end_us(each_tracing_data->data[i].end);
        trace->set_stage_id(each_tracing_data->data[i].count);
        trace->set_stage_type(name);
        // 写入二维数组堆栈
        std::string combined_stack;
        for (uint32_t j = 0; j < each_tracing_data->data[i].stack_depth; j++) {
            if (!combined_stack.empty()) {
              combined_stack += "|";
            }
            combined_stack += each_tracing_data->data[i].stack[j];
        }
        trace->add_stack_frames()->assign(combined_stack);
        if (each_tracing_data->data[i].type == PAYLOAD_GC) {
          hook::GcDebugData* gc_debug = trace->mutable_gc_debug();
          gc_debug->set_collected(
              each_tracing_data->data[i].payload.gc_debug[0]);
          gc_debug->set_uncollectable(
              each_tracing_data->data[i].payload.gc_debug[1]);
        }
      }
      inner_index++;
    }
    for (auto each_tracing_data : holders)
      pytorch_tracing_library_->ReturnTracingData(each_tracing_data,
                                             PY_TRACING_EMPTY_POOL, name_index);
  }

  // 生成唯一 .timeline 文件路径。
  std::ostringstream oss;
  oss << dump_path << "/" << util::getUniqueFileNameByCluster(".timeline");
  std::string file_path(oss.str());

  // 以二进制格式写入 pytorch_trace_ 数据到 .timeline 文件。
  std::ofstream file(file_path, std::ios::binary | std::ios::out);
  if (!file) {
    STLOG(FATAL) << "Error opening file for writing";
    return;
  }
  std::string binary_message;
  pytorch_trace_.SerializeToString(&binary_message);
  file << binary_message;
  STLOG(INFO) << "Rank " << config::GlobalConfig::rank << " dump timeline to "
             << file_path;
}

void SysTrace::initSingleton() {
  // register env first
  util::REGISTER_ENV();
  instance_ = new SysTrace;

  instance_->startWork();

  std::atexit([] { delete instance_; });
}

void SysTrace::stopWork() noexcept {
  if (!config::GlobalConfig::enable) return;

  should_run_.store(false);
  STLOG(INFO) << "Stoping poller thread...";
  if (event_poller_.joinable()) {
    event_poller_.join();
  }
  STLOG(INFO) << "Stoping Metrics manager";
  STLOG(INFO) << "Thread is stopped, exit process";
}

void SysTrace::doWork() {
  while (should_run_.load()) {

    if (loop_count_ >= 1000) {  // 1000 is for warmup
      if (PyTorchTrace::getInstance().triggerTrace()) {
        PyTorchTrace::getInstance().dumpPyTorchTracing();
      }
    }
    loop_count_++;
  }
}

SysTrace& SysTrace::getInstance() {
  std::call_once(init_flag_, &SysTrace::initSingleton);
  return *instance_;
}

void SysTrace::startWork() {
  // ===================================
  // Global config setup
  // ===================================
  config::setUpConfig();
  if (!config::GlobalConfig::enable) return;
  int local_rank = config::GlobalConfig::local_rank;
  setLoggingPath();

  PyTorchTrace::getInstance();
  // barrier here wait daemon started
  util::InterProcessBarrier(config::GlobalConfig::local_world_size,
                            local_rank, "start_work_barrier");
  
  should_run_.store(true);

#ifdef _GNU_SOURCE
  // pthread_setname_np is not posix
  event_poller_ = std::thread(&SysTrace::doWork, this);
  auto handle = event_poller_.native_handle();
  pthread_setname_np(handle, "systrace_poller");
#endif
}
} 