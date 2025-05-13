#include "util.h"
#include "constant.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <thread>

namespace systrace
{
namespace util
{
namespace fs_utils
{
int ensureDirExists(const std::string &path)
{
    std::filesystem::path dir_path(path);
    try
    {
        if (!std::filesystem::exists(dir_path))
        {
            std::filesystem::create_directories(dir_path);
        }
        if (!std::filesystem::is_directory(dir_path))
        {
            LOG(ERROR) << "Path exists but is not a directory: " << path
                       << std::endl;
            return 1;
        }
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        LOG(ERROR) << "Create dir " << path << " error: " << e.what()
                   << std::endl;
        return 1;
    }
    return 0;
}

std::vector<std::string> split(const std::string &str,
                               const std::string &delimiter)
{
    if (delimiter.empty())
    {
        LOG(ERROR) << "Empty delimiter provided to split()" << std::endl;
        return {str};
    }

    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos)
    {
        tokens.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start, end));
    return tokens;
}

std::string getUniqueFileNameByCluster(const std::string &suffix)
{
    try
    {
        std::ostringstream oss;
        oss << std::setw(5) << std::setfill('0') << config::GlobalConfig::rank
            << "-" << std::setw(5) << std::setfill('0')
            << config::GlobalConfig::world_size << suffix;
        return oss.str();
    }
    catch (const std::exception &e)
    {
        LOG(ERROR) << "Filename generation failed: " << e.what() << std::endl;
        return "error_" + std::to_string(std::time(nullptr)) + suffix;
    }
}
} // namespace fs_utils
namespace config
{

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

void setUpConfig() { setUpGlobalConfig(); }

void setUpGlobalConfig()
{
    LOG(INFO) << "Initializing global configuration" << std::endl;

    try
    {
        GlobalConfig::rank = env::EnvVarRegistry::GetEnvVar<int>("RANK");
        GlobalConfig::job_name = env::EnvVarRegistry::GetEnvVar<std::string>(
            "ENV_ARGO_WORKFLOW_NAME");
        GlobalConfig::local_rank =
            env::EnvVarRegistry::GetEnvVar<int>("LOCAL_RANK");
        GlobalConfig::local_world_size =
            env::EnvVarRegistry::GetEnvVar<int>("LOCAL_WORLD_SIZE");
        GlobalConfig::world_size =
            env::EnvVarRegistry::GetEnvVar<int>("WORLD_SIZE");
        GlobalConfig::debug_mode =
            env::EnvVarRegistry::GetEnvVar<bool>("SYSTRACE_DEBUG_MODE");

        GlobalConfig::rank_str =
            "[RANK " + std::to_string(GlobalConfig::rank) + "] ";

        // 设备检测部分
        const std::string dev_path = "/dev/davinci";
        for (uint64_t device_index = 0; device_index < 16; device_index++)
        {
            std::filesystem::path dev(dev_path + std::to_string(device_index));
            if (std::filesystem::exists(dev))
            {
                GlobalConfig::all_devices.push_back(device_index);
                if (GlobalConfig::local_rank == 0)
                {
                    LOG(INFO) << "Found device: " << dev << std::endl;
                }
            }
        }

        std::sort(GlobalConfig::all_devices.begin(),
                  GlobalConfig::all_devices.end());

        if (GlobalConfig::all_devices.empty())
        {
            GlobalConfig::enable = false;
            LOG(WARNING) << "No devices found, disabling tracing" << std::endl;
        }

        if (GlobalConfig::local_world_size != GlobalConfig::all_devices.size())
        {
            LOG(WARNING) << "Local world size mismatch, disabling hook"
                         << std::endl;
            GlobalConfig::enable = false;
        }

        if (GlobalConfig::debug_mode)
        {
            GlobalConfig::enable = true;
            LOG(INFO) << "Debug mode enabled, overriding checks" << std::endl;
        }

        LOG(INFO) << "Global configuration initialized successfully"
                  << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG(ERROR) << "Global config initialization failed: " << e.what()
                   << std::endl;
        throw;
    }
}

} // namespace config

void REGISTER_ENV()
{
    auto validateVarName = [](const std::string &name)
    {
        if (name.empty() || !isalpha(name[0]))
        {
            throw std::invalid_argument("Invalid env var name: " + name);
        }
        for (char c : name)
        {
            if (!isalnum(c) && c != '_')
            {
                throw std::invalid_argument(
                    "Invalid character in env var name: " + name);
            }
        }
    };

    try
    {
        validateVarName("ENV_ARGO_WORKFLOW_NAME");
        REGISTER_ENV_VAR("ENV_ARGO_WORKFLOW_NAME",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        validateVarName("SYSTRACE_SYMS_FILE");
        REGISTER_ENV_VAR("SYSTRACE_SYMS_FILE",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        validateVarName("SYSTRACE_LOGGING_DIR");
        REGISTER_ENV_VAR("SYSTRACE_LOGGING_DIR",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        validateVarName("SYSTRACE_HOST_TRACING_FUNC");
        REGISTER_ENV_VAR("SYSTRACE_HOST_TRACING_FUNC",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        REGISTER_ENV_VAR("RANK", 0);
        REGISTER_ENV_VAR("LOCAL_RANK", 0);
        REGISTER_ENV_VAR("LOCAL_WORLD_SIZE", 1);
        REGISTER_ENV_VAR("WORLD_SIZE", 1);

        REGISTER_ENV_VAR("SYSTRACE_DEBUG_MODE", false);
        REGISTER_ENV_VAR("SYSTRACE_LOGGING_APPEND", false);
    }
    catch (const std::exception &e)
    {
        LOG(ERROR) << "Environment variable registration failed: " << e.what()
                   << std::endl;
        throw;
    }
}

} // namespace util
} // namespace systrace