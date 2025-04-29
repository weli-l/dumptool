#pragma once
#include <algorithm>

namespace systrace
{
namespace constant
{

struct TorchTraceConstant
{
  public:
    static constexpr int DEFAULT_TRACE_COUNT = 1000;
    static constexpr std::string_view DEFAULT_TRACE_DUMP_PATH =
        "/home/timeline";
};

} // namespace constant
} // namespace systrace