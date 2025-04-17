#include "hook.h"
#include "../../include/common/macro.h"
#include "../src/trace/systrace_manager.h"
#ifdef __cplusplus
extern "C" {
#endif

__attribute__((constructor)) void init() {
    std::cout << "[SYSTRACE] Initializing hook" << std::endl;
    ::systrace::SysTrace::getInstance();
    std::cout << "[SYSTRACE] SysTrace instance created" << std::endl;
}

EXPOSE_API 
aclError aclrtMapMem(void* virPtr, size_t size, size_t offset, 
                    aclrtDrvMemHandle handle, uint64_t flags) {
    try {
        std::cout << "[SYSTRACE] Entering aclrtMapMem" << std::endl;
        SETUP_DLSYM(aclrtMapMem);
        if (!::systrace::util::config::GlobalConfig::enable)
            return orig_aclrtMapMem(virPtr, size, offset, handle, flags);
        ::systrace::SysTrace::getInstance();   
        std::cout << "[SYSTRACE] SysTrace instance created" << std::endl;
        } catch (...) {
            std::cout << "[SYSTRACE] Exception in aclrtMapMem" << std::endl;
            return orig_aclrtMapMem(virPtr, size, offset, handle, flags);
        }
    std::cout << "[SYSTRACE] Calling original aclrtMapMem" << std::endl;
    return orig_aclrtMapMem(virPtr, size, offset, handle, flags);
}

EXPOSE_API 
aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle, 
                         uint32_t blockDim,
                         const void* argsData,
                         size_t argsSize,
                         aclrtStream stream) {
    try {
        std::cout << "[SYSTRACE] Entering aclrtLaunchKernel" << std::endl;
        SETUP_DLSYM(aclrtLaunchKernel);
        if (!::systrace::util::config::GlobalConfig::enable)
            return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
        ::systrace::SysTrace::getInstance();   
        std::cout << "[SYSTRACE] SysTrace instance created" << std::endl;
        } catch (...) {
            std::cout << "[SYSTRACE] Exception in aclrtLaunchKernel" << std::endl;
            return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
        }
    std::cout << "[SYSTRACE] Calling original aclrtLaunchKernel" << std::endl;
    return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
}

#ifdef __cplusplus
}
#endif