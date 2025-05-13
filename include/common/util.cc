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

int CreateDirectoryIfNotExists(const std::string &path)
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
            LOG(ERROR) << "Path exists but is not a directory: " << path;
            return 1;
        }
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        LOG(ERROR) << "Failed to create directory " << path << ": " << e.what();
        return 1;
    }
    return 0;
}

std::string GenerateClusterUniqueFilename(const std::string &suffix)
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
        LOG(ERROR) << "Filename generation failed: " << e.what();
        return "error_" + std::to_string(std::time(nullptr)) + suffix;
    }
}

} // namespace fs_utils

namespace config
{

class DeviceManager
{
  public:
    static std::vector<uint64_t> DetectAvailableDevices()
    {
        std::vector<uint64_t> devices;
        const std::string dev_path = "/dev/davinci";

        for (uint64_t device_index = 0; device_index < 16; device_index++)
        {
            std::filesystem::path dev(dev_path + std::to_string(device_index));
            if (std::filesystem::exists(dev))
            {
                devices.push_back(device_index);
                if (GlobalConfig::local_rank == 0)
                {
                    LOG(INFO) << "Found device: " << dev;
                }
            }
        }

        std::sort(devices.begin(), devices.end());
        return devices;
    }
};

uint32_t GlobalConfig::rank{0};
uint32_t GlobalConfig::local_rank{0};
uint32_t GlobalConfig::world_size{0};
uint32_t GlobalConfig::local_world_size{0};
std::string GlobalConfig::job_name("");
std::string GlobalConfig::rank_str("");
bool GlobalConfig::enable{true};
std::vector<uint64_t> GlobalConfig::all_devices;

void InitializeGlobalConfiguration()
{
    LOG(INFO) << "Initializing global configuration";

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
        GlobalConfig::rank_str =
            "[RANK " + std::to_string(GlobalConfig::rank) + "] ";

        GlobalConfig::all_devices = DeviceManager::DetectAvailableDevices();

        if (GlobalConfig::all_devices.empty())
        {
            GlobalConfig::enable = false;
            LOG(WARNING) << "No devices found, disabling tracing";
        }

        if (GlobalConfig::local_world_size != GlobalConfig::all_devices.size())
        {
            LOG(WARNING) << "Local world size mismatch, disabling hook";
            GlobalConfig::enable = false;
        }

        LOG(INFO) << "Global configuration initialized successfully";
    }
    catch (const std::exception &e)
    {
        LOG(ERROR) << "Global config initialization failed: " << e.what();
        throw;
    }
}

} // namespace config

namespace environment
{

bool IsValidEnvironmentVariableName(const std::string &name)
{
    if (name.empty() || !isalpha(name[0]))
    {
        return false;
    }

    for (char c : name)
    {
        if (!isalnum(c) && c != '_')
        {
            return false;
        }
    }
    return true;
}

void RegisterRequiredEnvironmentVariables()
{
    try
    {
        if (!IsValidEnvironmentVariableName("ENV_ARGO_WORKFLOW_NAME"))
        {
            throw std::invalid_argument(
                "Invalid env var name: ENV_ARGO_WORKFLOW_NAME");
        }
        REGISTER_ENV_VAR("ENV_ARGO_WORKFLOW_NAME",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        if (!IsValidEnvironmentVariableName("SYSTRACE_SYMS_FILE"))
        {
            throw std::invalid_argument(
                "Invalid env var name: SYSTRACE_SYMS_FILE");
        }
        REGISTER_ENV_VAR("SYSTRACE_SYMS_FILE",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        if (!IsValidEnvironmentVariableName("SYSTRACE_LOGGING_DIR"))
        {
            throw std::invalid_argument(
                "Invalid env var name: SYSTRACE_LOGGING_DIR");
        }
        REGISTER_ENV_VAR("SYSTRACE_LOGGING_DIR",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        if (!IsValidEnvironmentVariableName("SYSTRACE_HOST_TRACING_FUNC"))
        {
            throw std::invalid_argument(
                "Invalid env var name: SYSTRACE_HOST_TRACING_FUNC");
        }
        REGISTER_ENV_VAR("SYSTRACE_HOST_TRACING_FUNC",
                         env::EnvVarRegistry::STRING_DEFAULT_VALUE);

        REGISTER_ENV_VAR("RANK", 0);
        REGISTER_ENV_VAR("LOCAL_RANK", 0);
        REGISTER_ENV_VAR("LOCAL_WORLD_SIZE", 1);
        REGISTER_ENV_VAR("WORLD_SIZE", 1);
        REGISTER_ENV_VAR("SYSTRACE_LOGGING_APPEND", false);
    }
    catch (const std::exception &e)
    {
        LOG(ERROR) << "Environment variable registration failed: " << e.what();
        throw;
    }
}

} // namespace environment

void InitializeSystemUtilities()
{
    environment::RegisterRequiredEnvironmentVariables();
    config::InitializeGlobalConfiguration();
}

} // namespace util
} // namespace systrace