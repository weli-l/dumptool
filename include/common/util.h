#pragma once

#include "logging.h"
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace systrace
{
namespace util
{
namespace config
{

struct GlobalConfig
{
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

} // namespace config
} // namespace util
} // namespace systrace

namespace systrace
{
namespace util
{
namespace fs_utils
{

std::string getUniqueFileNameByCluster(const std::string &suffix);
int ensureDirExists(const std::string &path);
std::vector<std::string> split(const std::string &str,
                               const std::string &delimiter);

} // namespace fs_utils
} // namespace util
} // namespace systrace

namespace systrace
{
namespace util
{
namespace resource
{

class ScopeGuard
{
  public:
    explicit ScopeGuard(std::function<void()> cb) : cb_(cb) {}
    ~ScopeGuard() { cb_(); }

  private:
    std::function<void()> cb_;
};

template <typename T> class TimerPool
{
  public:
    TimerPool() = default;
    TimerPool(const TimerPool &) = delete;
    TimerPool &operator=(const TimerPool &) = delete;

    template <bool Create = true> T *getObject()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty())
        {
            if constexpr (Create)
                return new T();
            return nullptr;
        }

        T *obj = pool_.front();
        pool_.pop_front();
        return obj;
    }

    void returnObject(T *obj, int *size)
    {
        *size = 0;
        if (obj)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push_back(obj);
            *size = pool_.size();
        }
    }

  private:
    std::deque<T *> pool_;
    std::mutex mutex_;
};

} // namespace resource
} // namespace util
} // namespace systrace

namespace systrace
{
namespace util
{
namespace env
{

class EnvVarRegistry
{
  public:
    using VarType = std::variant<int, bool, std::string>;
    static constexpr std::string_view STRING_DEFAULT_VALUE = "NOT_SET";
    static constexpr int INT_DEFAULT_VALUE = 0;
    static constexpr bool BOOL_DEFAULT_VALUE = false;

    static inline VarType convert_to_variant(const std::string_view &sv)
    {
        return std::string(sv);
    }

    static inline VarType convert_to_variant(const char *s)
    {
        return std::string(s);
    }

    template <typename T> static inline VarType convert_to_variant(const T &val)
    {
        return val;
    }

    static void RegisterEnvVar(const std::string &name, VarType default_value)
    {
        auto &registry = GetRegistry();
        std::string str_val = std::visit(
            [](const auto &value) -> std::string
            {
                std::stringstream ss;
                ss << value;
                return ss.str();
            },
            default_value);
        LOG(INFO) << "[ENV] Register ENV " << name << " with default "
                  << str_val << std::endl;
        registry[name] = default_value;
    }

    template <typename T, bool Print = true>
    static T GetEnvVar(const std::string &name)
    {
        std::cout << "[ENV] GetEnvVar " << name << std::endl;
        auto &registry = GetRegistry();
        bool has_env = true;

        if (auto it = registry.find(name); it != registry.end())
        {
            auto result = getEnvInner<T>(name, &has_env);
            if (has_env)
            {
                if constexpr (Print)
                    LOG(INFO) << "[ENV] Get " << name << "=" << result
                              << " from environment" << std::endl;
                return result;
            }

            if (const T *result_p = std::get_if<T>(&it->second))
            {
                if constexpr (Print)
                    LOG(INFO) << "[ENV] Get " << name << "=" << *result_p
                              << " from register default" << std::endl;
                return *result_p;
            }
            else
            {
                if constexpr (Print)
                    LOG(FATAL)
                        << "[ENV] Wrong data type in `GetEnvVar`" << std::endl;
            }
        }
        else
        {
            auto result = getEnvInner<T>(name, &has_env);
            if (has_env)
            {
                if constexpr (Print)
                    LOG(INFO) << "[ENV] Get " << name << "=" << result
                              << " from environment" << std::endl;
                return result;
            }
        }

        auto result = getDefault<T>();
        if constexpr (Print)
            LOG(WARNING) << "[ENV] Get not register env " << name << "="
                         << result << " from default" << std::endl;
        return result;
    }

  private:
    template <typename T>
    static T getEnvInner(std::string env_name, bool *has_env)
    {
        const char *env = std::getenv(env_name.c_str());
        if (!env)
        {
            *has_env = false;
            return T{};
        }

        if constexpr (std::is_same_v<T, int>)
        {
            return std::atoi(env);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return std::atoi(env) != 0;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return std::string(env);
        }
        else
        {
            static_assert(std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                              std::is_same_v<T, std::string>,
                          "Unsupported type");
            return T{};
        }
    }

    template <typename T> static T getDefault()
    {
        if constexpr (std::is_same_v<T, int>)
        {
            return env::EnvVarRegistry::INT_DEFAULT_VALUE;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return env::EnvVarRegistry::BOOL_DEFAULT_VALUE;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return std::string(env::EnvVarRegistry::STRING_DEFAULT_VALUE);
        }
        else
        {
            static_assert(std::is_same_v<T, int> || std::is_same_v<T, bool> ||
                              std::is_same_v<T, std::string>,
                          "Unsupported type");
            return T{};
        }
    }

    static std::unordered_map<std::string, VarType> &GetRegistry()
    {
        static std::unordered_map<std::string, VarType> registry;
        return registry;
    }
};

#define REGISTER_ENV_VAR(name, value)                                          \
    ::systrace::util::env::EnvVarRegistry::RegisterEnvVar(                     \
        name,                                                                  \
        ::systrace::util::env::EnvVarRegistry::convert_to_variant(value))

void REGISTER_ENV();

} // namespace env
} // namespace util
} // namespace systrace