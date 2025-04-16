#include "hook.h"
#include "../../include/common/macro.h"
#include "../src/trace/systrace_manager.h"
#ifdef __cplusplus
extern "C" {
#endif

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