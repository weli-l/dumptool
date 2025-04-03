#pragma once

#include <string>
#include <vector>

#include "pytorch_tracing_data.h"
#include "../library_loader.h"

namespace systrace {
namespace pytorch_tracing {

class PyTorchTracingLibrary : public LibraryLoader {
 public:
  PyTorchTracingLibrary(const std::string&);
  using SysTraceRegisterTracingFunc = void (*)(const char**, int, char**);
  using GetFullTracingDataArrayFunc = PyTorchTracingDataArray* (*)(int);
  using GetPartialTracingDataArrayFunc = PyTorchTracingDataArray* (*)(int);
  using ReturnTracingDataArrayFunc = void (*)(PyTorchTracingDataArray*, int,
                                              int);
  using SwitchTracingFunc = void (*)(int);
  using GetTracingCountFunc = int64_t (*)(int);

  std::vector<std::string> Register(const std::vector<std::string>& names);
  PyTorchTracingDataArray* GetFullTracingData(int);
  PyTorchTracingDataArray* GetPartialTracingData(int);
  void ReturnTracingData(PyTorchTracingDataArray* data, int type, int name);
  void SwitchTracing(int flag);

 private:
  SysTraceRegisterTracingFunc register_tracing_;
  GetFullTracingDataArrayFunc get_tracing_data_;
  GetPartialTracingDataArrayFunc get_partial_tracing_data_;
  ReturnTracingDataArrayFunc return_tracing_data_;
  SwitchTracingFunc switch_tracing_;
  GetTracingCountFunc get_tracing_count_;
};

}  // namespace pytorch_tracing
}  // namespace systrace