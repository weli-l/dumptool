#pragma once
#include <dlfcn.h>

#include <functional>
#include <string>

#include "../../include/common/macro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int aclError;
typedef void *aclrtStream;
typedef void* aclrtFuncHandle;
typedef void* aclrtDrvMemHandle;

typedef aclError (*aclrtMapMemFn)(void*, size_t, size_t, aclrtDrvMemHandle, uint64_t);
typedef aclError (*aclrtLaunchKernelFn)(aclrtFuncHandle, uint32_t, const void*, size_t, aclrtStream);

extern aclrtMapMemFn orig_aclrtMapMem;
extern aclrtLaunchKernelFn orig_aclrtLaunchKernel;

EXPOSE_API aclError aclrtMapMem(void* virPtr, size_t size, size_t offset, 
                              aclrtDrvMemHandle handle, uint64_t flags);
EXPOSE_API aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle, 
                                    uint32_t blockDim,
                                    const void* argsData,
                                    size_t argsSize,
                                    aclrtStream stream);
aclrtMapMemFn orig_aclrtMapMem = nullptr;
aclrtLaunchKernelFn orig_aclrtLaunchKernel = nullptr;

#ifdef __cplusplus
}
#endif