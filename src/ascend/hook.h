#pragma once
#include <dlfcn.h>
#include <functional>
#include <string>
#include "../../include/common/macro.h"

typedef int aclError;
typedef void* aclrtStream;
typedef void* aclrtFuncHandle;
typedef void* aclrtDrvMemHandle;

#ifdef __cplusplus
extern "C" {
#endif

extern void* ascend_hal_handle;

typedef aclError (*aclInitFn)(const char*);
extern aclInitFn orig_aclInit;
aclError aclInit(const char* configPath);

typedef aclError (*aclrtMapMemFn)(void*, size_t, size_t, aclrtDrvMemHandle, uint64_t);
extern aclrtMapMemFn orig_aclrtMapMem;
aclError aclrtMapMem(void* virPtr, size_t size, size_t offset, 
                   aclrtDrvMemHandle handle, uint64_t flags);

typedef aclError (*aclrtLaunchKernelFn)(aclrtFuncHandle, int, void**, size_t*, 
                                      aclrtStream, void*, void*);
extern aclrtLaunchKernelFn orig_aclrtLaunchKernel;
aclError aclrtLaunchKernel(aclrtFuncHandle func, int workDim, void** workGroup, 
                         size_t* localWorkSize, aclrtStream stream, 
                         void* event, void* config);

#ifdef __cplusplus
}
#endif