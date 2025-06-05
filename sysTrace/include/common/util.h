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

struct GlobalConfig {
    uint32_t rank{0};
    uint32_t local_rank{0};
    uint32_t world_size{0};
    uint32_t local_world_size{0};
    std::string job_name;
    bool enable{true};
    std::vector<uint64_t> devices;
    std::string rank_str;

    static GlobalConfig& Instance() {
        static GlobalConfig instance;
        return instance;
    }

private:
    GlobalConfig() = default;
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
template <typename T> class TimerPool
{
  public:
    TimerPool() = default;
    TimerPool(const TimerPool &) = delete;
    TimerPool &operator=(const TimerPool &) = delete;

    template <bool Create = true> T *getObject()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pool_.empty())
        {
            T *obj = pool_.front();
            pool_.pop_front();
            return obj;
        }

        if constexpr (Create)
        {
            return new T();
        }
        return nullptr;
    }

    void returnObject(T *obj, int *size)
    {
        if (!obj)
        {
            if (size)
                *size = 0;
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push_back(obj);
        if (size)
            *size = static_cast<int>(pool_.size());
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto obj : pool_)
        {
            delete obj;
        }
        pool_.clear();
    }

    ~TimerPool() { clear(); }

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
    template <typename T> static T GetEnvVar(const std::string &name)
    {
        static_assert(is_supported_type<T>(),
                      "Unsupported type for environment variable");

        auto &registry = GetRegistry();
        bool has_env = false;

        // Try to get from environment first
        T result = getEnvInner<T>(name, &has_env);
        if (has_env)
        {
            LOG(INFO) << "[ENV] Get " << name << "=" << result
                      << " from environment" << std::endl;
            return result;
        }

        // Try to get from registered defaults
        if (auto it = registry.find(name); it != registry.end())
        {
            if (const T *val = std::get_if<T>(&it->second))
            {
                LOG(INFO) << "[ENV] Get " << name << "=" << *val
                          << " from register default" << std::endl;
                return *val;
            }
            LOG(FATAL) << "[ENV] Wrong data type in `GetEnvVar`" << std::endl;
        }

        // Fall back to static default
        result = getDefault<T>();
        LOG(WARNING) << "[ENV] Get not register env " << name << "=" << result
                     << " from default" << std::endl;
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
    template <typename T> static constexpr bool is_supported_type()
    {
        return std::is_same_v<T, int> || std::is_same_v<T, bool> ||
               std::is_same_v<T, std::string>;
    }

    static std::string toLower(const std::string &str)
    {
        std::string lower;
        lower.reserve(str.size());
        std::transform(str.begin(), str.end(), std::back_inserter(lower),
                       [](unsigned char c) { return std::tolower(c); });
        return lower;
    }

    // 值解析器
    template <typename T> static T parseEnvValue(const char *env)
    {
        if constexpr (std::is_same_v<T, int>)
        {
            try
            {
                return std::stoi(env);
            }
            catch (...)
            {
                return INT_DEFAULT_VALUE;
            }
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            std::string lower = toLower(env);
            if (lower == "true" || lower == "1")
                return true;
            if (lower == "false" || lower == "0")
                return false;
            return std::stoi(env) != 0;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return env;
        }
    }

    // Get value from real environment
    template <typename T>
    static T getEnvInner(const std::string &env_name, bool *has_env)
    {
        const char *env = std::getenv(env_name.c_str());
        if (!env)
        {
            *has_env = false;
            return {};
        }

        *has_env = true;
        return parseEnvValue<T>(env);
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
    }

    // Static registry accessor
    static std::unordered_map<std::string, VarType> &GetRegistry()
    {
        static std::unordered_map<std::string, VarType> registry;
        return registry;
    }

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
};

#define REGISTER_ENV_VAR(name, value)                                          \
    ::systrace::util::env::EnvVarRegistry::RegisterEnvVar(                     \
        name,                                                                  \
        ::systrace::util::env::EnvVarRegistry::convert_to_variant(value))

void REGISTER_ENV();

} // namespace env
void InitializeSystemUtilities();
} // namespace util
} // namespace systrace