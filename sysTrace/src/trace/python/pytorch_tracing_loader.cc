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
    const std::string err =
        "libsysTrace.so, skip recording python gc in timeline ";
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(library_handle_, "systrace_register_tracing",
                                  register_tracing_, TracingRegistrationFunc,
                                  err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        library_handle_, "systrace_get_full_pytorch_tracing_data_array",
        get_tracing_data_, DataArrayRetrievalAllFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        library_handle_, "systrace_return_pytorch_tracing_data_array",
        return_tracing_data_, DataArrayReleaseFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        library_handle_, "systrace_get_partial_pytorch_tracing_data_array",
        get_partial_tracing_data_, GetPartialTracingDataArrayPartFunc, err);
    is_usable_ = true;
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

PyTorchTracingDataArray *PyTorchTracingLibrary::RetrieveAllTracingData(int name)
{
    if (is_usable_)
    {
        return get_tracing_data_(name);
    }
    return nullptr;
}

PyTorchTracingDataArray *
PyTorchTracingLibrary::RetrievePartialTracingData(int name)
{
    if (is_usable_)
    {
        return get_partial_tracing_data_(name);
    }
    return nullptr;
}

void PyTorchTracingLibrary::ReleaseTracingData(PyTorchTracingDataArray *data,
                                               int type, int name)
{
    if (is_usable_ && data)
        return_tracing_data_(data, type, name);
}

} // namespace pytorch_tracing
} // namespace systrace