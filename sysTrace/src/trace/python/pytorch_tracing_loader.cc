#include "pytorch_tracing_loader.h"
#include "../../../include/common/logging.h"
#include <cstring>
#include <dlfcn.h>

namespace systrace
{
namespace pytorch_tracing
{

PyTorchTracingLibrary::PyTorchTracingLibrary(const std::string &library_path)
    : DynamicLibraryLoader(library_path), register_tracing_(nullptr),
      get_tracing_data_(nullptr), get_partial_tracing_data_(nullptr),
      return_tracing_data_(nullptr)
{
    if (library_handle_) {
        InitializeSymbols();
    }
}

void PyTorchTracingLibrary::InitializeSymbols() {
    std::vector<SymbolConfig> configs = {
        {"systrace_register_tracing", 
         [this]() { return reinterpret_cast<void*>(&register_tracing_); },
         "TracingRegistrationFunc"},
         
        {"systrace_get_full_pytorch_tracing_data_array",
         [this]() { return reinterpret_cast<void*>(&get_tracing_data_); },
         "DataArrayRetrievalAllFunc"},
         
        {"systrace_return_pytorch_tracing_data_array",
         [this]() { return reinterpret_cast<void*>(&return_tracing_data_); },
         "DataArrayReleaseFunc"},
         
        {"systrace_get_partial_pytorch_tracing_data_array",
         [this]() { return reinterpret_cast<void*>(&get_partial_tracing_data_); },
         "GetPartialTracingDataArrayPartFunc"}
    };

    is_usable_ = std::all_of(configs.begin(), configs.end(),
        [this](const SymbolConfig& config) {
            return LoadSymbol(config);
        });
}

bool PyTorchTracingLibrary::LoadSymbol(const SymbolConfig& config) {
    void* symbol = dlsym(library_handle_, config.name);
    if (!symbol) {
        STLOG(WARNING) << "Failed to load symbol: " << config.name
                      << " (type: " << config.type_name << "), error: " << dlerror();
        return false;
    }
    
    *reinterpret_cast<void**>(config.loader()) = symbol;
    return true;
}

std::vector<std::string>
PyTorchTracingLibrary::Register(const std::vector<std::string> &names)
{
    if (!is_usable_)
        return {};
    std::vector<std::string> result;
    char **errors = (char **)malloc(names.size() * sizeof(char *));
    std::memset(errors, 0, names.size() * sizeof(char *));

    std::vector<const char *> c_str_array;
    for (const auto &str : names)
    {
        c_str_array.push_back(str.c_str());
    }
    register_tracing_(c_str_array.data(), c_str_array.size(), errors);
    for (size_t i = 0; i < names.size(); i++)
    {
        if (errors[i])
        {
            result.push_back(std::string(errors[i]));
            free(errors[i]);
        }
    }

    free(errors);
    return result;
}

PyTorchTracingDataArray* PyTorchTracingLibrary::RetrieveAllTracingData(int name) {
    return is_usable_ ? get_tracing_data_(name) : nullptr;
}

PyTorchTracingDataArray* PyTorchTracingLibrary::RetrievePartialTracingData(int name) {
    return is_usable_ ? get_partial_tracing_data_(name) : nullptr;
}

void PyTorchTracingLibrary::ReleaseTracingData(PyTorchTracingDataArray* data, int type, int name) {
    if (is_usable_ && data) {
        return_tracing_data_(data, type, name);
    }
}

} // namespace pytorch_tracing
} // namespace systrace