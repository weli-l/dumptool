#include "pytorch_tracing_data.h"

#ifdef __cplusplus
extern "C"
{
#endif

    __attribute__((visibility("default"))) PyTorchTracingDataArray *
    systrace_get_empty_pytorch_tracing_data_array(int);
    __attribute__((visibility("default"))) PyTorchTracingDataArray *
    systrace_get_full_pytorch_tracing_data_array(int);

    __attribute__((visibility("default"))) PyTorchTracingDataArray *
    systrace_get_partial_pytorch_tracing_data_array(int);

    __attribute__((visibility("default"))) void
    systrace_return_pytorch_tracing_data_array(PyTorchTracingDataArray *,
                                               int type, int name);
    __attribute__((visibility("default"))) void
    systrace_register_tracing(const char **, int, char **);
    __attribute__((visibility("default"))) void
    systrace_switch_pytorch_tracing(int s);
    __attribute__((visibility("default"))) int64_t
    systrace_get_pytorch_tracing_count(int);

    __attribute__((visibility("default"))) void systrace_tracing_debug();
#ifdef __cplusplus
}
#endif