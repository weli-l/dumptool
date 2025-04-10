#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <variant>
#include <vector>

enum LogLevel {
    INFO,
    WARNING,
    ERROR,
    FATAL
};

// 自定义 LOG 宏
#define LOG(level) \
    if (level == INFO) std::cerr << "[INFO] "; \
    else if (level == WARNING) std::cerr << "[WARNING] "; \
    else if (level == ERROR) std::cerr << "[ERROR] "; \
    else if (level == FATAL) std::cerr << "[FATAL] "; \
    std::cerr

namespace bip = boost::interprocess;

namespace systrace {
namespace util {
namespace config {
struct GlobalConfig {
  static uint32_t rank;
  static uint32_t local_rank;
  static uint32_t local_world_size;
  static uint32_t world_size;
  static std::string job_name;
  static std::string rank_str;
  static bool enable;
  static std::vector<uint64_t> all_devices;
  static bool debug_mode;
  static std::unordered_map<std::string, std::string> dlopen_path;
};

void setUpConfig();
void setUpGlobalConfig();
}  // namespace config
namespace detail {
struct InterProcessBarrierImpl {
  struct Inner {
    alignas(8) bool val;
    Inner(bool val) : val(val) {}
    void reset(bool value) { val = value; }
  };

  InterProcessBarrierImpl(std::string name, int world_size, int rank);
  ~InterProcessBarrierImpl();
  std::string name_;
};
}  // namespace detail

std::string getUniqueFileNameByCluster(const std::string& suffix);
void REGISTER_ENV();
void InterProcessBarrier(int world_size, int rank,
                         std::string name = "barrier");
std::vector<std::string> split(const std::string& str,
                               const std::string& delimiter);
//explicit：构造函数被标记为 explicit，以防止隐式类型转换。这确保了只有在显式调用构造函数时才会创建 ScopeGuard 对象。
//std::function<void()> cb：构造函数接受一个 std::function 对象作为参数，该对象封装了一个可调用的回调函数。这个回调函数将在 ScopeGuard 对象被销毁时执行。
//cb_(cb)：将传入的回调函数存储到成员变量 cb_ 中。
class ScopeGuard {
 public:
  explicit ScopeGuard(std::function<void()> cb) : cb_(cb) {} //当 ScopeGuard 对象被销毁时（即离开其作用域时），析构函数会被调用。

  ~ScopeGuard() { cb_(); }

 private:
  std::function<void()> cb_;
};

int ensureDirExists(const std::string& path);

namespace detail {
struct ShmSwitch {
  static constexpr std::string_view BarrierName = "shm_switch_barrier";
  static constexpr std::string_view ShmName = "ShmSwitch";
  static constexpr std::string_view ObjName = "ShmSwitchObj";
  alignas(8) char dump_path[1024];
  alignas(8) char oss_dump_args[4096];
  alignas(8) int start_dump;
  alignas(8) int64_t timestamp;
  alignas(8) bool reset_flag;
  void reset();
  void reset(const std::string& path, const std::string& oss_args, int64_t stamp);
  void reset(const std::string& path, const std::string& oss_args, int64_t stamp, bool reset_signal);
};
}  // namespace detail

template <typename T>
class ShmType {
 public:
  explicit ShmType(int local_world_size, int local_rank, bool main = true) {
    LOG(INFO) << "enter ShmType rank " << std::endl;
    shm_name_ = std::string(T::ShmName);
    std::string obj_name(T::ObjName);
    size_t total_size = sizeof(T) * 4;

    std::string barrier_name = std::string(T::BarrierName);
    if (main) {
      bip::shared_memory_object::remove(shm_name_.c_str());
      shm_area_ = new bip::managed_shared_memory(bip::create_only,
                                                 shm_name_.c_str(), total_size);
      obj_ = shm_area_->construct<T>(obj_name.c_str())();
      obj_->reset();
      LOG(INFO) << "ShmType rank " << local_rank
                << " create shared memory object " << shm_name_ << std::endl;
      InterProcessBarrier(local_world_size, local_rank, barrier_name.c_str());
    } else {
      LOG(INFO) << "ShmType rank " << local_rank
                << " open shared memory object " << shm_name_ << std::endl;
      InterProcessBarrier(local_world_size, local_rank, barrier_name.c_str());
      shm_area_ =
          new bip::managed_shared_memory(bip::open_only, shm_name_.c_str());
      auto find = shm_area_->find<T>(obj_name.c_str());
      if (find.first)
        obj_ = find.first;
      else {
        // never here
        LOG(INFO) << "rank " << local_rank << " do not found" << std::endl;
        std::abort();
      }
    }
  }
  ~ShmType() { bip::shared_memory_object::remove(shm_name_.c_str()); }

  T* getObj() { return obj_; }

 private:
  T* obj_;
  bip::managed_shared_memory* shm_area_;
  std::string shm_name_;
};

using ShmSwitch = ShmType<detail::ShmSwitch>;

template <typename T>
class TimerPool {
  /* This is a deque for pooling XpuTimer object.
   */
 public:
  TimerPool() = default;

  TimerPool(const TimerPool&) = delete;
  TimerPool& operator=(const TimerPool&) = delete;

  // getObject 和 returnObject，它们分别用于从对象池中获取对象和将对象返回到对象池中
  template <bool Create = true>
  T* getObject() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pool_.empty()) {
      if constexpr (Create) return new T();
      return nullptr;
    } else {
      T* obj = pool_.front();
      pool_.pop_front();
      return obj;
    }
  }

  // Return an object to the pool
  void returnObject(T* obj, int* size) {
    *size = 0;
    if (obj) {
      // 通过 std::lock_guard<std::mutex> 确保对对象池的操作是线程安全的。
      std::lock_guard<std::mutex> lock(mutex_);
      pool_.push_back(obj);
      *size = pool_.size();
    }
  }

 private:
  std::deque<T*> pool_;
  std::mutex mutex_;
};

class EnvVarRegistry {
 public:
  using VarType = std::variant<int, bool, std::string>;
  static constexpr std::string_view STRING_DEFAULT_VALUE = "NOT_SET";
  static constexpr int INT_DEFAULT_VALUE = 0;
  static constexpr bool BOOL_DEFAULT_VALUE = false;

  static inline VarType convert_to_variant(const std::string_view& sv) {
    return std::string(sv);
  }

  static inline VarType convert_to_variant(const char* s) {
    return std::string(s);
  }

  template <typename T>
  static inline VarType convert_to_variant(const T& val) {
    return val;
  }

  static void RegisterEnvVar(const std::string& name, VarType default_value) {
    auto& registry = GetRegistry();
    std::string str_val = std::visit(
        [](const auto& value) -> std::string {
          std::stringstream ss;
          ss << value;
          return ss.str();
        },
        default_value);
    LOG(INFO) << "[ENV] Register ENV " << name << " with default " << str_val << std::endl;
    registry[name] = default_value;
  }

  template <typename T, bool Print = true>
  static T GetEnvVar(const std::string& name) {
    auto& registry = GetRegistry();
    bool has_env = true;
    // if has register env, we get env as below
    // 1. return value if find in environment
    // 2. return value from config file
    // 3. return registered default value
    if (auto it = registry.find(name); it != registry.end()) {
      auto result = getEnvInner<T>(name, &has_env);
      if (has_env) {
        if constexpr (Print)
          LOG(INFO) << "[ENV] Get " << name << "=" << result
                    << " from environment" << std::endl;
        return result;
      }

      auto& pt = GetPtree();
      if (auto it = pt.find(name); it != pt.not_found()) {
        auto result = pt.get<T>(name);
        if constexpr (Print)
          LOG(INFO) << "[ENV] Get " << name << "=" << result << " from config" << std::endl;
        return result;
      }
      if (const T* result_p = std::get_if<T>(&it->second)) {
        if constexpr (Print)
          LOG(INFO) << "[ENV] Get " << name << "=" << *result_p
                    << " from register default" << std::endl;
        return *result_p;
      } else {
        // GetEnvVar is a internal api, so you should verify it, it not, we
        // abort
        if constexpr (Print)
          LOG(FATAL) << "[ENV] Wrong data type in `GetEnvVar`" << std::endl;
      }
    } else {
      auto result = getEnvInner<T>(name, &has_env);
      if (has_env) {
        if constexpr (Print)
          LOG(INFO) << "[ENV] Get " << name << "=" << result
                    << " from environment" << std::endl;
        return result;
      }
    }
    // if not register value, return default value for different dtype
    auto result = getDefault<T>();
    if constexpr (Print)
      LOG(WARNING) << "[ENV] Get not register env " << name << "=" << result
                   << " from default" << std::endl;
    return result;
  }

  static std::string getLibPath(const std::string& lib_name) {
    const std::string& env_name = "XPU_TIMER_" + lib_name + "_LIB_PATH";
    auto lib_path = GetEnvVar<std::string>(env_name);
    if (lib_path != STRING_DEFAULT_VALUE) {
      LOG(INFO) << "[ENV] Get lib path for dlopen " << lib_name << "="
                << lib_path << " from env " << env_name << std::endl;
      return lib_path;
    }
    lib_path = config::GlobalConfig::dlopen_path[lib_name];
    if (lib_path.empty()) {
      std::cerr << "[ENV] Can't find any " << lib_name
                << " lib path from default" << std::endl;
      std::exit(1);
    }
    LOG(INFO) << "[ENV] Get lib path for dlopen " << lib_name << "=" << lib_path
              << " by default value. You can change it via env " << env_name << std::endl;

    return lib_path;
  }

 private:
  template <typename T>
  static T getEnvInner(std::string env_name, bool* has_env) {
    const char* env = std::getenv(env_name.c_str());
    if (!env) {
      *has_env = false;
      return T{};
    }
    if constexpr (std::is_same_v<T, int>) {
      return std::atoi(env);
    } else if constexpr (std::is_same_v<T, bool>) {
      return std::atoi(env) != 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return std::string(env);
    } else {
      static_assert(std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                        std::is_same_v<T, std::string>,
                    "Unsupported type");
      return T{};  // never goes here
    }
  }

  template <typename T>
  static T getDefault() {
    if constexpr (std::is_same_v<T, int>) {
      return EnvVarRegistry::INT_DEFAULT_VALUE;
    } else if constexpr (std::is_same_v<T, bool>) {
      return EnvVarRegistry::BOOL_DEFAULT_VALUE;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return std::string(EnvVarRegistry::STRING_DEFAULT_VALUE);
    } else {
      static_assert(std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                        std::is_same_v<T, std::string>,
                    "Unsupported type");
      return T{};  // never goes here
    }
  }

  static std::unordered_map<std::string, VarType>& GetRegistry() {
    static std::unordered_map<std::string, VarType> registry;
    return registry;
  }

  static boost::property_tree::ptree& GetPtree() {
    static boost::property_tree::ptree pt;
    static bool pt_init_flag = false;

    if (!pt_init_flag) {
      pt_init_flag = true;
      const char* config_path = std::getenv("XPU_TIMER_CONFIG");
      if (config_path && config_path != EnvVarRegistry::STRING_DEFAULT_VALUE) {
        if (std::filesystem::exists(config_path))
          boost::property_tree::ini_parser::read_ini(config_path, pt);
        else
          LOG(WARNING) << "XPU_TIMER_CONFIG config " << config_path
                       << " is not exists, ignore it" << std::endl;
      }
    }
    return pt;
  }
};

#define REGISTER_ENV_VAR(name, value)                \
  ::systrace::util::EnvVarRegistry::RegisterEnvVar( \
      name, ::systrace::util::EnvVarRegistry::convert_to_variant(value))

}  // namespace util
}  // namespace systrace