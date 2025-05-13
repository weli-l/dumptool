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

LibraryLoader::~LibraryLoader()
{
    if (handle_)
    {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

void LibraryLoader::LoadLibrary()
{
    if (handle_)
    {
        STLOG(WARNING) << "Library already loaded: " << library_path_;
        return;
    }
    dlerror();

    handle_ = dlopen(library_path_.c_str(), RTLD_LAZY);
    if (!handle_)
    {
        const char *err_msg = dlerror();
        STLOG(WARNING) << "Failed to load library: "
                       << (err_msg ? err_msg : "Unknown error");
        can_use_ = false;
        return;
    }

    can_use_ = true;
}

} // namespace systrace