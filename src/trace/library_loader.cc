#include "library_loader.h"
#include <dlfcn.h>

#include "../../include/common/logging.h"

namespace systrace
{
LibraryLoader::LibraryLoader(const std::string &lib_name)
    : handle_(nullptr), can_use_(false), library_path_(lib_name)
{
    LoadLibrary();
}

void LibraryLoader::LoadLibrary()
{
    handle_ = dlopen(library_path_.c_str(), RTLD_LAZY);
    if (!handle_)
    {
        STLOG(WARNING) << "Failed to load library: " << dlerror();
        can_use_ = false;
        return;
    }
}
} // namespace systrace