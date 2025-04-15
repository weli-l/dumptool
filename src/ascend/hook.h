#pragma once
#include <dlfcn.h>
#include <acl/acl.h>

#include <functional>
#include <string>

#include "../../include/common/macro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef aclError (*aclLaunchKernelFn)(aclrtFuncHandle, uint32_t, const void*, size_t, aclrtStream);

extern aclLaunchKernelFn orig_aclrtLaunchKernel;

EXPOSE_API aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle, 
                                    uint32_t blockDim,
                                    const void* argsData,
                                    size_t argsSize,
                                    aclrtStream stream);
aclLaunchKernelFn orig_aclrtLaunchKernel = nullptr;


#ifdef __cplusplus
}
#endif