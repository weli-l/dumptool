#include "pytorch_tracing_loader.h"

#include <dlfcn.h>

#include <cstring>

#include "../../../include/common/logging.h"
#include "../../../include/common/macro.h"

namespace systrace
{
namespace pytorch_tracing
{

PyTorchTracingLibrary::PyTorchTracingLibrary(const std::string &library_path)
    : LibraryLoader(library_path), register_tracing_(nullptr),
      get_tracing_data_(nullptr), get_partial_tracing_data_(nullptr),
      return_tracing_data_(nullptr), switch_tracing_(nullptr)
{
    const std::string err =
        "libsysTrace.so, skip recording python gc in timeline ";
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(handle_, "systrace_register_tracing",
                                  register_tracing_,
                                  SysTraceRegisterTracingFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        handle_, "systrace_get_full_pytorch_tracing_data_array",
        get_tracing_data_, GetFullTracingDataArrayFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        handle_, "systrace_return_pytorch_tracing_data_array",
        return_tracing_data_, ReturnTracingDataArrayFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(
        handle_, "systrace_get_partial_pytorch_tracing_data_array",
        get_partial_tracing_data_, GetPartialTracingDataArrayFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(handle_, "systrace_switch_pytorch_tracing",
                                  switch_tracing_, SwitchTracingFunc, err);
    SETUP_SYMBOL_FOR_LOAD_LIBRARY(handle_, "systrace_get_pytorch_tracing_count",
                                  get_tracing_count_, GetTracingCountFunc, err);
    can_use_ = true;
}

std::vector<std::string>
PyTorchTracingLibrary::Register(const std::vector<std::string> &names)
{
    if (!can_use_)
        return {};
    std::vector<std::string> result;
    // this take own ship of all errors
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

PyTorchTracingDataArray *PyTorchTracingLibrary::GetFullTracingData(int name)
{
    if (can_use_)
    {
        return get_tracing_data_(name);
    }
    return nullptr;
}

PyTorchTracingDataArray *PyTorchTracingLibrary::GetPartialTracingData(int name)
{
    if (can_use_)
    {
        return get_partial_tracing_data_(name);
    }
    return nullptr;
}

void PyTorchTracingLibrary::ReturnTracingData(PyTorchTracingDataArray *data,
                                              int type, int name)
{
    if (can_use_ && data)
        return_tracing_data_(data, type, name);
}

void PyTorchTracingLibrary::SwitchTracing(int flag)
{
    if (can_use_)
        switch_tracing_(flag);
}

} // namespace pytorch_tracing
} // namespace systrace