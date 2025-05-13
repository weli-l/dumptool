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
    explicit LibraryLoader(const std::string &library_path);
    virtual ~LibraryLoader();

    bool IsLoaded() const { return handle_ != nullptr && can_use_; }

    void *GetHandle() const { return handle_; }
};

} // namespace systrace
