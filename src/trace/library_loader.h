#pragma once

#include "../../include/common/util.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace systrace
{
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