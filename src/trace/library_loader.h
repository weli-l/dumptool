#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "../../include/common/constant.h"
#include "../../include/common/util.h"
namespace systrace
{

namespace config = systrace::util::config;
namespace util = systrace::util;
namespace constant = systrace::constant;

class LibraryLoader
{
  protected:
    void *handle_;
    bool can_use_;
    const std::string library_path_;
    void LoadLibrary();

  public:
    LibraryLoader(const std::string &library_path);
};

} // namespace systrace