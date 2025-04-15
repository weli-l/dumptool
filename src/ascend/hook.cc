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
        if (!::systrace::util::config::GlobalConfig::enable)
            return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
        ::systrace::SysTrace::getInstance();   
        } catch (...) {
            return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
        }
    return orig_aclrtLaunchKernel(funcHandle, blockDim, argsData, argsSize, stream);
}

#ifdef __cplusplus
}
#endif