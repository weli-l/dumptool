#include "util.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "constant.h"

namespace systrace {
namespace util {

namespace detail {

void ShmSwitch::reset() {
  // Call the parameterized reset() with default values
  this->reset(std::string(constant::TorchTraceConstant::DEFAULT_TRACE_DUMP_PATH),
              std::string(constant::TorchTraceConstant::DEFAULT_TRACE_DUMP_PATH),
              0);
}

void ShmSwitch::reset(const std::string& path, const std::string& oss_args,
                      int64_t stamp) {
  if (path.length() >= sizeof(dump_path) || 
      oss_args.length() >= sizeof(oss_dump_args)) {
    LOG(ERROR) << "Path or args too long for buffer (max: " 
               << sizeof(dump_path)-1 << " bytes)" << std::endl;
    return;
  }
  
  strncpy(dump_path, path.data(), sizeof(dump_path)-1);
  dump_path[sizeof(dump_path)-1] = '\0';

  strncpy(oss_dump_args, oss_args.data(), sizeof(oss_dump_args)-1);
  oss_dump_args[sizeof(oss_dump_args)-1] = '\0';

  start_dump = 1;
  timestamp = stamp;
  reset_flag = false;  // Default reset_flag to false
}

void ShmSwitch::reset(const std::string& path, const std::string& oss_args,
                      int64_t stamp, bool reset_signal) {
  // Reuse the 3-parameter version and just set the flag
  this->reset(path, oss_args, stamp);
  reset_flag = reset_signal;
}

InterProcessBarrierImpl::InterProcessBarrierImpl(std::string name,
                                                 int world_size, int rank)
    : name_(name) {
  bip::managed_shared_memory managed_shm(bip::open_or_create, name.c_str(),
                                         4096);  // one page is enough
  LOG(INFO) << "Barrier name in shm is " << name << std::endl;
  LOG(INFO) << "World size " << world_size << " rank " << rank << std::endl;
  InterProcessBarrierImpl::Inner*
      barriers[world_size];  // world size is small, allocate on stack is safe
  std::string barrier_name("InterProcessBarrierImpl" + std::to_string(rank));
  auto this_bar =
      managed_shm.find<InterProcessBarrierImpl::Inner>(barrier_name.c_str());
  if (this_bar.first) {
    barriers[rank] = this_bar.first;
  } else {
    barriers[rank] = managed_shm.construct<InterProcessBarrierImpl::Inner>(
        barrier_name.c_str())(false);
  }
  int index = 0;
  uint64_t try_count = 0;
  while (index < world_size) {
    std::string name = "InterProcessBarrierImpl" + std::to_string(index);
    auto this_bar =
        managed_shm.find<InterProcessBarrierImpl::Inner>(name.c_str());
    if (this_bar.first) {
      barriers[index] = this_bar.first;
      LOG(INFO) << "rank " << rank << " found index is " << index << std::endl;
      index++;
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      try_count++;
      if (try_count > 10000 * 10) {
        LOG(INFO) << "Rank " << rank << " waiting 10s for rank " << index
                  << " to create barrier obj in " << name_ << std::endl;
        try_count = 0;
      }
    }
  }
  // reset all state
  for (auto barrier : barriers) barrier->reset(false);
  try_count = 0;
  bool ready = false;
  // clang-format off
	// https://www.boost.org/doc/libs/1_84_0/doc/html/interprocess/some_basic_explanations.html#interprocess.some_basic_explanations.persistence
	// Note on Shared Memory Cleanup:
	// In Boost 1.84.0, shared memory resources (kernel or filesystem level) may not be correctly cleaned up 
	// if a process exits unexpectedly, despite utilizing RAII for resource management. This can lead to 
	// scenarios where shared memory resources are not properly initialized upon subsequent process startups.
	//
	// Solution for Barrier Synchronization:
	// To address potential initialization issues, we implement eventual consistency for barrier synchronization. 
	// Initially, all participating processes (ranks) reset their respective barrier flags to false before entering 
	// the barrier loop. Within this loop, each rank then independently marks itself as ready. This approach 
	// ensures robust barrier synchronization even if shared memory initialization is inconsistent.
  // clang-format on
  while (!ready) {
    ready = true;
    for (int i = 0; i < world_size; i++) {
      ready = barriers[i]->val && ready;
      if (try_count > 10000 && !barriers[i]->val) {
        LOG(INFO) << "Waiting rank " << i << " sleep 1s" << std::endl;
        try_count = 0;
      }
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    try_count++;
    // only set state in barrier operation
    barriers[rank]->reset(true);
  }
  LOG(INFO) << "Rank " << rank << " pass barrier " << name_ << std::endl;
}
InterProcessBarrierImpl::~InterProcessBarrierImpl() {
  bip::shared_memory_object::remove(name_.c_str());
}
}  // namespace detail

void InterProcessBarrier(int world_size, int rank, std::string name) {
  try {
    LOG(INFO) << "InterProcessBarrier name is " << name << std::endl;
    detail::InterProcessBarrierImpl(name, world_size, rank);
  } catch (const std::exception& e) {
    LOG(ERROR) << "InterProcessBarrier failed: " << e.what() << std::endl;
    throw;
  }
}

int ensureDirExists(const std::string& path) {
  std::filesystem::path dir_path(path);
  try {
    if (!std::filesystem::exists(dir_path)) {
      std::filesystem::create_directories(dir_path);
    }
    // Verify directory is actually accessible
    if (!std::filesystem::is_directory(dir_path)) {
      LOG(ERROR) << "Path exists but is not a directory: " << path << std::endl;
      return 1;
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG(ERROR) << "Create dir " << path << " error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

std::vector<std::string> split(const std::string& str,
                               const std::string& delimiter) {
  if (delimiter.empty()) {
    LOG(ERROR) << "Empty delimiter provided to split()" << std::endl;
    return {str};
  }

  std::vector<std::string> tokens;
  size_t start = 0;
  size_t end = str.find(delimiter);
  while (end != std::string::npos) {
    tokens.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }
  tokens.push_back(str.substr(start, end));
  return tokens;
}

namespace config {
/*
 * ===================================
 * GlobalConfig
 * ===================================
 */
uint32_t GlobalConfig::rank{0};
uint32_t GlobalConfig::local_rank{0};
uint32_t GlobalConfig::world_size{0};
uint32_t GlobalConfig::local_world_size{0};
std::string GlobalConfig::job_name("");
std::string GlobalConfig::rank_str("");
bool GlobalConfig::enable{true};
std::vector<uint64_t> GlobalConfig::all_devices;
bool GlobalConfig::debug_mode{false};
std::unordered_map<std::string, std::string> GlobalConfig::dlopen_path;

// std::atomic<bool> GlobalConfig::initialized{false};
// std::mutex GlobalConfig::init_mutex;

void setUpConfig() {
  setUpGlobalConfig();
}

void setUpGlobalConfig() {
  LOG(INFO) << "Entering setUpGlobalConfig()" << std::endl;

  try {
    LOG(INFO) << "Getting RANK environment variable" << std::endl;
    GlobalConfig::rank = EnvVarRegistry::GetEnvVar<int>("RANK");
    LOG(INFO) << "RANK = " << GlobalConfig::rank << std::endl;

    // LOG(INFO) << "Getting ENV_ARGO_WORKFLOW_NAME environment variable" << std::endl;
    // GlobalConfig::job_name = EnvVarRegistry::GetEnvVar<std::string>("ENV_ARGO_WORKFLOW_NAME");
    // LOG(INFO) << "ENV_ARGO_WORKFLOW_NAME = " << GlobalConfig::job_name << std::endl;

    LOG(INFO) << "Getting LOCAL_RANK environment variable" << std::endl;
    GlobalConfig::local_rank = EnvVarRegistry::GetEnvVar<int>("LOCAL_RANK");
    LOG(INFO) << "LOCAL_RANK = " << GlobalConfig::local_rank << std::endl;

    LOG(INFO) << "Getting LOCAL_WORLD_SIZE environment variable" << std::endl;
    GlobalConfig::local_world_size = EnvVarRegistry::GetEnvVar<int>("LOCAL_WORLD_SIZE");
    LOG(INFO) << "LOCAL_WORLD_SIZE = " << GlobalConfig::local_world_size << std::endl;

    LOG(INFO) << "Getting WORLD_SIZE environment variable" << std::endl;
    GlobalConfig::world_size = EnvVarRegistry::GetEnvVar<int>("WORLD_SIZE");
    LOG(INFO) << "WORLD_SIZE = " << GlobalConfig::world_size << std::endl;

    // GlobalConfig::rank_str = "[RANK " + std::to_string(GlobalConfig::rank) + "] ";
    // LOG(INFO) << "rank_str = " << GlobalConfig::rank_str << std::endl;

    LOG(INFO) << "Getting SYSTRACE_DEBUG_MODE environment variable" << std::endl;
    GlobalConfig::debug_mode = EnvVarRegistry::GetEnvVar<bool>("SYSTRACE_DEBUG_MODE");
    LOG(INFO) << "SYSTRACE_DEBUG_MODE = " << GlobalConfig::debug_mode << std::endl;

    std::string dev_path = "/dev/davinci";
    LOG(INFO) << "Starting device search in " << dev_path << "0-15" << std::endl;

    for (uint64_t device_index = 0; device_index < 16; device_index++) {
      std::filesystem::path dev(dev_path + std::to_string(device_index));
      LOG(INFO) << "Checking device: " << dev << std::endl;
      if (std::filesystem::exists(dev)) {
        GlobalConfig::all_devices.push_back(device_index);
        LOG(INFO) << "Found device: " << dev << std::endl;
        if (GlobalConfig::local_rank == 0) {
          LOG(INFO) << "[ENV] Found device " << dev << std::endl;
        }
      } else {
        LOG(INFO) << "Device not found: " << dev << std::endl;
      }
    }
    
    LOG(INFO) << "Found " << GlobalConfig::all_devices.size() << " devices" << std::endl;
    if (GlobalConfig::all_devices.empty()) {
      GlobalConfig::enable = false;
      LOG(WARNING) << "[ENV] No devices found, disabling tracing" << std::endl;
    } else {
      LOG(INFO) << "Device list:" << std::endl;
      for (auto dev : GlobalConfig::all_devices) {
        LOG(INFO) << "  " << dev_path << dev << std::endl;
      }
    }
    
    std::sort(GlobalConfig::all_devices.begin(), GlobalConfig::all_devices.end());
    LOG(INFO) << "Sorted device list:" << std::endl;
    for (auto dev : GlobalConfig::all_devices) {
      LOG(INFO) << "  " << dev_path << dev << std::endl;
    }

    LOG(INFO) << "Comparing local_world_size (" << GlobalConfig::local_world_size 
              << ") with found devices (" << GlobalConfig::all_devices.size() << ")" << std::endl;
    if (GlobalConfig::local_world_size != GlobalConfig::all_devices.size()) {
      LOG(WARNING) << "[ENV] local world size(" << GlobalConfig::local_world_size
                << ") is not equal to found devices("
                << GlobalConfig::all_devices.size() << ") disabling hook" << std::endl;
      GlobalConfig::enable = false;
    }

    LOG(INFO) << "Current enable flag: " << GlobalConfig::enable << std::endl;
    if (!GlobalConfig::enable) {
      LOG(INFO) << "[ENV] Not all devices are used, disable hook" << std::endl;
    }
    if (GlobalConfig::debug_mode) {
      GlobalConfig::enable = true;
      LOG(INFO) << "[ENV] Debug mode is on, ignore all checks" << std::endl;
    }
    
    LOG(INFO) << "Final enable flag: " << GlobalConfig::enable << std::endl;
    LOG(INFO) << "Exiting setUpGlobalConfig() successfully" << std::endl;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to initialize global config: " << e.what() << std::endl;
    throw;
  }
}

}  // namespace config

std::string getUniqueFileNameByCluster(const std::string& suffix) {
  try {
    std::ostringstream oss;
    std::string rank_str = std::to_string(config::GlobalConfig::rank);
    std::string world_size_str = std::to_string(config::GlobalConfig::world_size);
    
    // Ensure we have at least 5 digits with leading zeros
    oss << std::setw(5) << std::setfill('0') << rank_str << "-"
        << std::setw(5) << std::setfill('0') << world_size_str << suffix;
    
    return oss.str();
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to generate unique filename: " << e.what() << std::endl;
    return "error_" + std::to_string(std::time(nullptr)) + suffix;
  }
}

void REGISTER_ENV() {
  auto validateVarName = [](const std::string& name) {
    if (name.empty() || !isalpha(name[0])) {
      throw std::invalid_argument("Invalid env var name: " + name);
    }
    for (char c : name) {
      if (!isalnum(c) && c != '_') {
        throw std::invalid_argument("Invalid character in env var name: " + name);
      }
    }
  };

  try {
    validateVarName("ENV_ARGO_WORKFLOW_NAME");
    REGISTER_ENV_VAR("ENV_ARGO_WORKFLOW_NAME",
                    EnvVarRegistry::STRING_DEFAULT_VALUE);
    
    validateVarName("SYSTRACE_SYMS_FILE");
    REGISTER_ENV_VAR("SYSTRACE_SYMS_FILE",
                    util::EnvVarRegistry::STRING_DEFAULT_VALUE);
    
    validateVarName("SYSTRACE_LOGGING_DIR");
    REGISTER_ENV_VAR("SYSTRACE_LOGGING_DIR",
                    EnvVarRegistry::STRING_DEFAULT_VALUE);
    
    REGISTER_ENV_VAR("SYSTRACE_LOGGING_APPEND", false);
    REGISTER_ENV_VAR("RANK", 0);
    REGISTER_ENV_VAR("LOCAL_RANK", 0);
    REGISTER_ENV_VAR("LOCAL_WORLD_SIZE", 1);
    REGISTER_ENV_VAR("WORLD_SIZE", 1);
    REGISTER_ENV_VAR("SYSTRACE_DEBUG_MODE", false);
    
    validateVarName("SYSTRACE_HOST_TRACING_FUNC");
    REGISTER_ENV_VAR("SYSTRACE_HOST_TRACING_FUNC",
                    EnvVarRegistry::STRING_DEFAULT_VALUE);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to register environment variables: " << e.what() << std::endl;
    throw;
  }
}

}  // namespace util
}  // namespace systrace