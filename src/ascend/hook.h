#pragma once
#include <dlfcn.h>
#include <functional>
#include <string>

#ifdef __cplusplus
extern "C"
{
#endif
#define EXPOSE_API __attribute__((visibility("default")))

    typedef int aclError;
    typedef void *aclrtStream;
    typedef void *aclrtFuncHandle;
    typedef void *aclrtDrvMemHandle;

    typedef aclError (*aclInitFn)(const char *);
    typedef aclError (*aclrtMapMemFn)(void *, size_t, size_t, aclrtDrvMemHandle,
                                      uint64_t);
    typedef aclError (*aclrtLaunchKernelFn)(aclrtFuncHandle, int, void **,
                                            size_t *, aclrtStream, void *,
                                            void *);

    extern void *ascend_hal_handle;
    extern aclInitFn orig_aclInit;
    extern aclrtMapMemFn orig_aclrtMapMem;
    extern aclrtLaunchKernelFn orig_aclrtLaunchKernel;

    aclError aclInit(const char *configPath);
    aclError aclrtMapMem(void *virPtr, size_t size, size_t offset,
                         aclrtDrvMemHandle handle, uint64_t flags);
    aclError aclrtLaunchKernel(aclrtFuncHandle func, int workDim,
                               void **workGroup, size_t *localWorkSize,
                               aclrtStream stream, void *event, void *config);

    static void *g_hal_lib = nullptr;
    aclInitFn orig_aclInit = nullptr;
    aclrtMapMemFn orig_aclrtMapMem = nullptr;
    aclrtLaunchKernelFn orig_aclrtLaunchKernel = nullptr;
#ifdef __cplusplus
}
#endif