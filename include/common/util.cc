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
#include <iomanip>

#include "constant.h"

namespace systrace {
namespace util {

namespace detail {

void ShmSwitch::reset() {
  this->reset(std::string(constant::TorchTraceConstant::DEFAULT_TRACE_DUMP_PATH),
              std::string(constant::TorchTraceConstant::DEFAULT_TRACE_DUMP_PATH),
              0);
}

void ShmSwitch::reset(const std::string& path, const std::string& oss_args,
                      int64_t stamp) {
  if (path.length() >= sizeof(dump_path) || 
      oss_args.length() >= sizeof(oss_dump_args)) {
    LOG(ERROR) << "Path or args too long for buffer (max: " 
               << sizeof(dump_path)-1 << " bytes)";
    return;
  }
  
  strncpy(dump_path, path.data(), sizeof(dump_path)-1);
  dump_path[sizeof(dump_path)-1] = '\0';

  strncpy(oss_dump_args, oss_args.data(), sizeof(oss_dump_args)-1);
  oss_dump_args[sizeof(oss_dump_args)-1] = '\0';

  start_dump = 1;
  timestamp = stamp;
  reset_flag = false;
}

void ShmSwitch::reset(const std::string& path, const std::string& oss_args,
                      int64_t stamp, bool reset_signal) {
  this->reset(path, oss_args, stamp);
  reset_flag = reset_signal;
}

InterProcessBarrierImpl::InterProcessBarrierImpl(std::string name,
                                               int world_size, int rank)
    : name_(name) {
  constexpr auto kTimeout = std::chrono::seconds(30);
  auto start = std::chrono::steady_clock::now();

  // Ensure cleanup even if exception occurs
  struct ShmGuard {
    std::string name;
    ~ShmGuard() { 
      bip::shared_memory_object::remove(name.c_str()); 
    }
  } guard{name};

  try {
    bip::managed_shared_memory managed_shm(bip::open_or_create, name.c_str(), 4096);
    LOG(INFO) << "Rank " << rank << " opened shm: " << name;

    // Initialize current rank's barrier
    std::string my_barrier_name = "InterProcessBarrierImpl" + std::to_string(rank);
    auto* my_barrier = managed_shm.find_or_construct<Inner>(my_barrier_name.c_str())(false);
    LOG(INFO) << "Rank " << rank << " initialized barrier: " << my_barrier_name;

    // Wait for other ranks
    for (int i = 0; i < world_size; ++i) {
      std::string target_name = "InterProcessBarrierImpl" + std::to_string(i);
      while (true) {
        if (std::chrono::steady_clock::now() - start > kTimeout) {
          throw std::runtime_error("Timeout waiting for rank " + std::to_string(i));
        }

        if (managed_shm.find<Inner>(target_name.c_str()).first) {
          LOG(INFO) << "Rank " << rank << " found barrier: " << target_name;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    // Synchronization logic
    bool ready = false;
    while (!ready) {
      if (std::chrono::steady_clock::now() - start > kTimeout) {
        throw std::runtime_error("Barrier synchronization timeout");
      }

      ready = true;
      for (int i = 0; i < world_size; ++i) {
        std::string target_name = "InterProcessBarrierImpl" + std::to_string(i);
        auto bar = managed_shm.find<Inner>(target_name.c_str());
        ready = ready && bar.first && bar.first->val;
      }
      
      if (!ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        managed_shm.find<Inner>(my_barrier_name.c_str()).first->reset(true);
      }
    }

    LOG(INFO) << "Rank " << rank << " passed barrier";
    guard.name.clear(); // Prevent cleanup on success
  } catch (const std::exception& e) {
    LOG(ERROR) << "Barrier failed: " << e.what();
    throw;
  }
}

InterProcessBarrierImpl::~InterProcessBarrierImpl() {
  if (!name_.empty()) {
    bip::shared_memory_object::remove(name_.c_str());
  }
}

}  // namespace detail

void InterProcessBarrier(int world_size, int rank, std::string name) {
  // Cleanup any previous shared memory first
  bip::shared_memory_object::remove(name.c_str());
  
  try {
    LOG(INFO) << "Initializing barrier: " << name 
             << " (world_size=" << world_size << ", rank=" << rank << ")";
    detail::InterProcessBarrierImpl(name, world_size, rank);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Barrier failed: " << e.what();
    throw;
  }
}

int ensureDirExists(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    LOG(ERROR) << "Failed to create directory " << path << ": " << ec.message();
    return 1;
  }
  return 0;
}

std::vector<std::string> split(const std::string& str,
                             const std::string& delimiter) {
  std::vector<std::string> tokens;
  size_t start = 0, end;
  while ((end = str.find(delimiter, start)) != std::string::npos) {
    tokens.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
  }
  tokens.push_back(str.substr(start));
  return tokens;
}

namespace config {

uint32_t GlobalConfig::rank{0};
uint32_t GlobalConfig::local_rank{0};
uint32_t GlobalConfig::world_size{0};
uint32_t GlobalConfig::local_world_size{0};
std::string GlobalConfig::job_name;
std::string GlobalConfig::rank_str;
bool GlobalConfig::enable{true};
std::vector<uint64_t> GlobalConfig::all_devices;
bool GlobalConfig::debug_mode{false};
std::unordered_map<std::string, std::string> GlobalConfig::dlopen_path;
void setUpConfig() {
  setUpGlobalConfig();
}

void setUpGlobalConfig() {
  try {
    GlobalConfig::world_size = EnvVarRegistry::GetEnvVar<int>("WORLD_SIZE");
    GlobalConfig::rank = EnvVarRegistry::GetEnvVar<int>("RANK");
    GlobalConfig::local_rank = EnvVarRegistry::GetEnvVar<int>("LOCAL_RANK");
    GlobalConfig::local_world_size = EnvVarRegistry::GetEnvVar<int>("LOCAL_WORLD_SIZE");
    
    // Validate ranks
    if (GlobalConfig::rank >= GlobalConfig::world_size) {
      throw std::runtime_error("Invalid RANK (must be < WORLD_SIZE)");
    }

    GlobalConfig::rank_str = "[RANK " + std::to_string(GlobalConfig::rank) + "] ";
    GlobalConfig::debug_mode = EnvVarRegistry::GetEnvVar<bool>("SYSTRACE_DEBUG_MODE");

    // Device detection
    std::string dev_path = "/dev/davinci";
    for (uint64_t i = 0; i < 16; ++i) {
      std::filesystem::path dev(dev_path + std::to_string(i));
      std::error_code ec;
      if (std::filesystem::exists(dev, ec)) {
        GlobalConfig::all_devices.push_back(i);
      } else if (ec) {
        LOG(WARNING) << "Error checking device " << dev << ": " << ec.message();
      }
    }

    // Enable/disable logic
    if (GlobalConfig::all_devices.empty()) {
      GlobalConfig::enable = false;
      LOG(WARNING) << "No devices found, disabling tracing";
    } else if (GlobalConfig::local_world_size != GlobalConfig::all_devices.size()) {
      GlobalConfig::enable = false;
      LOG(WARNING) << "Device count (" << GlobalConfig::all_devices.size() 
                  << ") != LOCAL_WORLD_SIZE (" << GlobalConfig::local_world_size 
                  << "), disabling tracing";
    }

    if (GlobalConfig::debug_mode) {
      GlobalConfig::enable = true;
      LOG(WARNING) << "Debug mode enabled - overriding safety checks";
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Config initialization failed: " << e.what();
    throw;
  }
}

}  // namespace config

std::string getUniqueFileNameByCluster(const std::string& suffix) {
  try {
    std::ostringstream oss;
    oss << std::setw(5) << std::setfill('0') << config::GlobalConfig::rank
        << "-" << std::setw(5) << std::setfill('0') << config::GlobalConfig::world_size
        << suffix;
    return oss.str();
  } catch (...) {
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
    // Required variables
    REGISTER_ENV_VAR("RANK", 0);
    REGISTER_ENV_VAR("WORLD_SIZE", 1);
    REGISTER_ENV_VAR("LOCAL_RANK", 0);
    REGISTER_ENV_VAR("LOCAL_WORLD_SIZE", 1);
    
    // Optional variables
    REGISTER_ENV_VAR("SYSTRACE_DEBUG_MODE", false);
    REGISTER_ENV_VAR("ENV_ARGO_WORKFLOW_NAME", "");
    REGISTER_ENV_VAR("SYSTRACE_HOST_TRACING_FUNC", "");
  } catch (const std::exception& e) {
    LOG(ERROR) << "Environment registration failed: " << e.what();
    throw;
  }
}

}  // namespace util
}  // namespace systrace