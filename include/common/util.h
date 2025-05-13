#pragma once

#include "logging.h"
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
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
};

void InitializeGlobalConfiguration();

} // namespace config

namespace fs_utils
{

std::string GenerateClusterUniqueFilename(const std::string &suffix);
int CreateDirectoryIfNotExists(const std::string &path);

} // namespace fs_utils

namespace resource
{

class ScopeGuard
{
  public:
    explicit ScopeGuard(std::function<void()> cb) : cb_(std::move(cb)) {}
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
            {
                return new T();
            }
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
            *size = static_cast<int>(pool_.size());
        }
    }

  private:
    std::deque<T *> pool_;
    std::mutex mutex_;
};

} // namespace resource

namespace env
{

class EnvVarRegistry
{
  public:
    using VarType = std::variant<int, bool, std::string>;

    static constexpr std::string_view STRING_DEFAULT_VALUE = "NOT_SET";
    static constexpr int INT_DEFAULT_VALUE = 0;
    static constexpr bool BOOL_DEFAULT_VALUE = false;

    // Register an env var with a default value
    static void RegisterEnvVar(const std::string &name, VarType default_value)
    {
        auto &registry = GetRegistry();
        LOG(INFO) << "[ENV] Register ENV " << name << " with default "
                  << VariantToString(default_value) << std::endl;
        registry[name] = std::move(default_value);
    }

    // Get an env var value, with optional printing
    template <typename T, bool Print = true>
    static T GetEnvVar(const std::string &name)
    {
        auto &registry = GetRegistry();
        bool has_env = false;

        // Try to get from environment first
        T result = getEnvInner<T>(name, &has_env);
        if (has_env)
        {
            if constexpr (Print)
                LOG(INFO) << "[ENV] Get " << name << "=" << result
                          << " from environment" << std::endl;
            return result;
        }

        // Try to get from registered defaults
        if (auto it = registry.find(name); it != registry.end())
        {
            if (const T *val = std::get_if<T>(&it->second))
            {
                if constexpr (Print)
                    LOG(INFO) << "[ENV] Get " << name << "=" << *val
                              << " from register default" << std::endl;
                return *val;
            }
            else
            {
                if constexpr (Print)
                    LOG(FATAL)
                        << "[ENV] Wrong data type in `GetEnvVar`" << std::endl;
            }
        }

        // Fall back to static default
        result = getDefault<T>();
        if constexpr (Print)
            LOG(WARNING) << "[ENV] Get not register env " << name << "="
                         << result << " from default" << std::endl;
        return result;
    }

    // Convert values into variant
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

  private:
    // Get value from real environment
    template <typename T>
    static T getEnvInner(const std::string &env_name, bool *has_env)
    {
        const char *env = std::getenv(env_name.c_str());
        if (!env)
        {
            *has_env = false;
            return T{};
        }
        *has_env = true;

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
            static_assert(always_false<T>::value, "Unsupported type");
        }
    }

    // Default values for fallback
    template <typename T> static T getDefault()
    {
        if constexpr (std::is_same_v<T, int>)
        {
            return INT_DEFAULT_VALUE;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return BOOL_DEFAULT_VALUE;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return std::string(STRING_DEFAULT_VALUE);
        }
        else
        {
            static_assert(always_false<T>::value, "Unsupported type");
        }
    }

    // Static registry accessor
    static std::unordered_map<std::string, VarType> &GetRegistry()
    {
        static std::unordered_map<std::string, VarType> registry;
        return registry;
    }

    // Convert variant to string (for logging)
    static std::string VariantToString(const VarType &var)
    {
        return std::visit(
            [](const auto &value)
            {
                std::stringstream ss;
                ss << value;
                return ss.str();
            },
            var);
    }

    // Helper for static_assert false on unsupported types
    template <typename> struct always_false : std::false_type
    {
    };
};

#define REGISTER_ENV_VAR(name, value)                                          \
    ::systrace::util::env::EnvVarRegistry::RegisterEnvVar(                     \
        name,                                                                  \
        ::systrace::util::env::EnvVarRegistry::convert_to_variant(value))

void REGISTER_ENV();

} // namespace env
} // namespace util
} // namespace systrace