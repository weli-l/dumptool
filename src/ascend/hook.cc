#include <iostream>
#include <cstdlib>
#include <dlfcn.h>
#include "hook.h"
#include "../../include/common/macro.h"
#include "../src/trace/systrace_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global variables for original function pointers
aclInitFn orig_aclInit = nullptr;
aclrtMapMemFn orig_aclrtMapMem = nullptr;
aclrtLaunchKernelFn orig_aclrtLaunchKernel = nullptr;

EXPOSE_API aclError aclInit(const char* configPath) {
    void *hal_lib = dlopen("libascendcl.so", RTLD_LAZY);
    if (!hal_lib) {
        fprintf(stderr, "Failed to dlopen target library: %s\n", dlerror());
        return -1;
    }
    
    orig_aclInit = (aclInitFn)dlsym(hal_lib, "aclInit");
    
    if (!orig_aclInit) {
        std::cout << "[Hook] failed to hook aclInit" << std::endl;
        return -1;
    }
    std::cout << "[Hook] successfully hooked aclInit func" << std::endl;
    ::systrace::SysTrace::getInstance();
    return orig_aclInit(configPath);
}

EXPOSE_API aclError aclrtMapMem(void* virPtr, size_t size, size_t offset, 
                              aclrtDrvMemHandle handle, uint64_t flags) {
    if (!orig_aclrtMapMem) {
        void *hal_lib = dlopen("libascendcl.so", RTLD_LAZY);
        if (!hal_lib) {
            fprintf(stderr, "Failed to dlopen target library: %s\n", dlerror());
            return -1;
        }
        
        orig_aclrtMapMem = (aclrtMapMemFn)dlsym(hal_lib, "aclrtMapMem");
        
        if (!orig_aclrtMapMem) {
            std::cout << "[Hook] failed to hook aclrtMapMem" << std::endl;
            return -1;
        }
        std::cout << "[Hook] successfully hooked aclrtMapMem func" << std::endl;
    }
    
    ::systrace::SysTrace::getInstance();
    return orig_aclrtMapMem(virPtr, size, offset, handle, flags);
}

EXPOSE_API aclError aclrtLaunchKernel(aclrtFuncHandle func, int workDim, void** workGroup, 
                                    size_t* localWorkSize, aclrtStream stream, 
                                    void* event, void* config) {
    if (!orig_aclrtLaunchKernel) {
        void *hal_lib = dlopen("libascendcl.so", RTLD_LAZY);
        if (!hal_lib) {
            fprintf(stderr, "Failed to dlopen target library: %s\n", dlerror());
            return -1;
        }
        
        orig_aclrtLaunchKernel = (aclrtLaunchKernelFn)dlsym(hal_lib, "aclrtLaunchKernel");
        
        if (!orig_aclrtLaunchKernel) {
            std::cout << "[Hook] failed to hook aclrtLaunchKernel" << std::endl;
            return -1;
        }
        std::cout << "[Hook] successfully hooked aclrtLaunchKernel func" << std::endl;
    }
    
    ::systrace::SysTrace::getInstance();
    return orig_aclrtLaunchKernel(func, workDim, workGroup, localWorkSize, stream, event, config);
}

#ifdef __cplusplus
}
#endif