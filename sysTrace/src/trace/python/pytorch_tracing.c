#include "pytorch_tracing.h"

#include <Python.h>
#include <frameobject.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "pytorch_tracing_data.h"
#include "uthash.h"
#include "../../../include/common/shared_constants.h"

typedef struct _frame PyFrameObject;
uint64_t getCodeOfFrame(PyFrameObject *frame);
static void capture_stack(PyFrameObject *frame,
                          PyTorchTracingData *trace_entry);
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11
#include <pyframe.h>
uint64_t getCodeOfFrame(PyFrameObject *frame)
{
    return (int64_t)(uintptr_t)PyFrame_GetCode(frame);
}
static void capture_stack(PyFrameObject *frame, PyTorchTracingData *trace_entry)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    int depth = 0;
    while (frame && depth < MAX_STACK_DEPTH)
    {
        PyCodeObject *code = PyFrame_GetCode(frame);
        if (!code)
        {
            break;
        }

        const char *name = PyUnicode_AsUTF8(code->co_name);
        const char *file = PyUnicode_AsUTF8(code->co_filename);
        int line = PyFrame_GetLineNumber(frame);

        snprintf(trace_entry->stack_info[depth], 256, "%s@%s:%d",
                 name ? name : "unknown", file ? file : "unknown", line);

        PyFrameObject *next_frame = PyFrame_GetBack(frame);
        Py_DECREF(code);
        frame = next_frame;

        depth++;
    }
    trace_entry->stack_depth = depth;
    PyGILState_Release(gstate);
}

#else
uint64_t getCodeOfFrame(PyFrameObject *frame)
{
    return (int64_t)(uintptr_t)(frame->f_code);
}
static void capture_stack(PyFrameObject *frame, PyTorchTracingData *trace_entry)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    int depth = 0;
    while (frame && depth < MAX_STACK_DEPTH)
    {
        snprintf(trace_entry->stack_info[depth], 256, "%s@%s:%d",
                 PyUnicode_AsUTF8(frame->f_code->co_name),
                 PyUnicode_AsUTF8(frame->f_code->co_filename),
                 PyFrame_GetLineNumber(frame));
        frame = frame->f_back;
        depth++;
    }
    trace_entry->stack_depth = depth;
    PyGILState_Release(gstate);
}

#endif

typedef struct
{
    int64_t py_code_address;
    const char *function_name;
    int tag_name;
    int is_native;
    UT_hash_handle hh;
} TracingFunction;

typedef struct
{
    int tag_name;
    PyTorchTracingDataArray *curr_data;
    int64_t count;
    const char *function_name;
} TracingData;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static TracingData *pytorch_tracing_data_array = NULL;

static TracingFunction *pytorch_tracing_func_map = NULL;
static int start_tracing = 1;
static int tracing_data_count = 0;

static int runPyTorchCodeGetAddress(const char *input, char **error_message,
                                    int64_t *code_address, int *is_native);
static uint64_t getMicrosecondTimestamp();
static TracingFunction *isTracedPyTorchFunction(PyFrameObject *frame);
static TracingData *getTracingData(int name);
static void addTracingData(int name, const char *func_name);
static int profiler(PyObject *obj, PyFrameObject *frame, int what,
                    PyObject *arg);

uint64_t getMicrosecondTimestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

Stagetype determine_stage_type(const char *function_name) {
    if (function_name == NULL) {
        return UNKNOWN;
    }

    if (strcmp(function_name, "GC") == 0) {
        return GC;
    }
    if (strcmp(function_name, "torch.utils.data.dataloader@_BaseDataLoaderIter@__next__") == 0) {
        return DATALOADER;
    }
    if (strcmp(function_name, "torch_npu@npu@synchronize") == 0 ||
        strcmp(function_name, "torch_npu.npu@Event@synchronize") == 0 ||
        strcmp(function_name, "torch_npu.npu@Event@wait") == 0 ||
        strcmp(function_name, "torch_npu.npu@Stream@synchronize") == 0 ||
        strcmp(function_name, "torch_npu.npu@Stream@wait_event") == 0 ||
        strcmp(function_name, "torch_npu.npu@Stream@wait_stream") == 0) {
        return SYNCHRONIZATION;
    }
    if (strcmp(function_name, "torch@autograd@backward") == 0 ||
        strcmp(function_name, "torch@autograd@grad") == 0) {
        return BACKWARD;
    }
    if (strcmp(function_name, "megatron.core.pipeline_parallel@schedules@forward_step") == 0) {
        return FORWARD;
    }
    if (strcmp(function_name, "megatron.core.pipeline_parallel@schedules@backward_step") == 0) {
        return BACKWARD;
    }
    return UNKNOWN;
}

TracingFunction *isTracedPyTorchFunction(PyFrameObject *frame)
{
    uint64_t code_address = getCodeOfFrame(frame);
    TracingFunction *traced_function = NULL;
    HASH_FIND(hh, pytorch_tracing_func_map, &code_address, sizeof(int64_t),
              traced_function);
    return traced_function;
}

static int profiler(PyObject *obj, PyFrameObject *frame, int what,
                    PyObject *arg)
{
    TracingFunction *func_data = isTracedPyTorchFunction(frame);
    if (!func_data)
        return 0;
    int tag_name = func_data->tag_name;
    int stage_type = determine_stage_type(func_data->function_name);
    if ((what == PyTrace_CALL) && start_tracing)
    {
        pthread_mutex_lock(&mutex);
        TracingData *tracing_data = getTracingData(tag_name);
        PyTorchTracingDataArray *curr_data = tracing_data->curr_data;
        if (curr_data->cur == PY_TRACING_BUFFER_SIZE)
        {
            systrace_return_pytorch_tracing_data_array(
                curr_data, PY_TRACING_READY_POOL, tag_name);
            tracing_data->curr_data =
                systrace_get_empty_pytorch_tracing_data_array(tag_name);
            curr_data = tracing_data->curr_data;
        }
        curr_data->data[curr_data->cur].start = getMicrosecondTimestamp();
        if (stage_type == DATALOADER) {
            global_stage_id++;
        }
        curr_data->data[curr_data->cur].stage_id = global_stage_id;
        curr_data->data[curr_data->cur].stage_type = stage_type;
        global_stage_type = stage_type;
        capture_stack(frame, &curr_data->data[curr_data->cur]);

        pthread_mutex_unlock(&mutex);
    }
    else if (what == PyTrace_RETURN)
    {
        pthread_mutex_lock(&mutex);
        TracingData *tracing_data = getTracingData(tag_name);
        if (start_tracing)
        {
            PyTorchTracingDataArray *curr_data = tracing_data->curr_data;
            curr_data->data[curr_data->cur].count = tracing_data->count;
            curr_data->data[curr_data->cur++].end = getMicrosecondTimestamp();
        }
        tracing_data->count++;
        pthread_mutex_unlock(&mutex);
    }
    return 0;
}

int runPyTorchCodeGetAddress(const char *code, char **error_message,
                             int64_t *address, int *is_native)
{
    char *input = strdup(code);
    char python_code[4096];
    PyObject *globals = NULL;
    PyObject *locals = NULL;

    *error_message = NULL;

    snprintf(python_code, sizeof(python_code),
             "if '@' in '%s':\n"
             "    tokens = '%s'.split('@')\n"
             "    if len(tokens) == 3:\n"
             "        exec(f'from {tokens[0]} import {tokens[1]} as mm')\n"
             "        obj = getattr(mm, tokens[2])\n"
             "    elif len(tokens) == 2:\n"
             "        exec(f'from {tokens[0]} import {tokens[1]} as obj')\n"
             "    else:\n"
             "        raise ValueError('Invalid input format')\n"
             "else:\n"
             "    obj = globals().get('%s')\n"
             "    if obj is None:\n"
             "        raise ValueError('Global object not found: %s')\n"

             "while hasattr(obj, '__wrapped__'):\n"
             "    obj = getattr(obj, '__wrapped__')\n"
             "if hasattr(obj, '__code__'):\n"
             "    address = id(obj.__code__)\n"
             "    is_native = 0\n"
             "else:\n"
             "    address = id(obj)\n"
             "    is_native = 1\n",
             input, input, input, input);
    int use_globals = strchr(input, '@') == NULL;
    if (use_globals)
    {
        globals = PyEval_GetGlobals();
        locals = PyEval_GetLocals();
    }
    else
    {
        globals = PyDict_New();
        locals = PyDict_New();
    }

    PyObject *result =
        PyRun_String(python_code, Py_file_input, globals, locals);

    if (result == NULL)
    {
        if (PyErr_Occurred())
        {
            PyObject *ptype, *pvalue, *ptraceback;
            PyErr_Fetch(&ptype, &pvalue, &ptraceback);
            PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

            PyObject *py_str = PyObject_Str(pvalue);
            const char *str_error = PyUnicode_AsUTF8(py_str);
            *error_message = strdup(str_error ? str_error : "Unknown error");

            Py_XDECREF(py_str);
            Py_XDECREF(ptype);
            Py_XDECREF(pvalue);
            Py_XDECREF(ptraceback);
        }
        else
        {
            *error_message = strdup("Unknown error occurred");
        }
        PyErr_Clear();
        if (!use_globals)
        {
            Py_DECREF(globals);
            Py_DECREF(locals);
        }
        free(input);
        return 1;
    }

    *address = PyLong_AsLongLong(PyDict_GetItemString(locals, "address"));
    *is_native = PyLong_AsLongLong(PyDict_GetItemString(locals, "is_native"));

    if (!use_globals)
    {
        Py_DECREF(globals);
        Py_DECREF(locals);
    }
    free(input);

    size_t msg_size =
        snprintf(NULL, 0, "Get __code__ attribute for '%s' OK", code) + 1;
    *error_message = (char *)malloc(msg_size);
    snprintf(*error_message, msg_size, "Get __code__ attribute for '%s' OK",
             code);
    return 0;
}

static TracingData *getTracingData(int name)
{
    return pytorch_tracing_data_array + name;
}

static void addTracingData(int name, const char *func_name)
{
    TracingData *v = getTracingData(name);
    v->tag_name = name;
    v->curr_data = systrace_get_empty_pytorch_tracing_data_array(name);
    v->function_name = strdup(func_name);
}

static void getGcInfo(PyTorchTracingData *data, PyObject *info)
{
    if (!PyDict_Check(info))
        return;
    PyObject *collected = PyDict_GetItemString(info, "collected");
    PyObject *uncollectable = PyDict_GetItemString(info, "uncollectable");

    if (collected && PyLong_Check(collected))
    {
        data->payload.gc_debug[0] = PyLong_AsLong(collected);
    }
    else
    {
        data->payload.gc_debug[0] = -1;
    }

    if (uncollectable && PyLong_Check(uncollectable))
    {
        data->payload.gc_debug[1] = PyLong_AsLong(uncollectable);
    }
    else
    {
        data->payload.gc_debug[1] = -1;
    }
}

static void gcCallback(PyObject *phase, PyObject *info)
{
    pthread_mutex_lock(&mutex);
    if (PyUnicode_CompareWithASCIIString(phase, "start") == 0 && start_tracing)
    {
        TracingData *tracing_data = getTracingData(PY_TRACING_GC);
        PyTorchTracingDataArray *curr_data = tracing_data->curr_data;
        if (curr_data->cur == PY_TRACING_BUFFER_SIZE)
        {
            systrace_return_pytorch_tracing_data_array(
                curr_data, PY_TRACING_READY_POOL, PY_TRACING_GC);
            tracing_data->curr_data =
                systrace_get_empty_pytorch_tracing_data_array(PY_TRACING_GC);
            curr_data = tracing_data->curr_data;
        }
        curr_data->data[curr_data->cur].start = getMicrosecondTimestamp();
        pthread_mutex_unlock(&mutex);
    }
    else if (PyUnicode_CompareWithASCIIString(phase, "stop") == 0)
    {
        TracingData *tracing_data = getTracingData(PY_TRACING_GC);
        if (start_tracing)
        {
            PyTorchTracingDataArray *curr_data = tracing_data->curr_data;
            if (start_tracing)
            {
                curr_data->data[curr_data->cur].count = tracing_data->count;
                curr_data->data[curr_data->cur].type = PAYLOAD_GC;
                getGcInfo(curr_data->data + curr_data->cur, info);
                curr_data->data[curr_data->cur++].end =
                    getMicrosecondTimestamp();
            }
            curr_data->data[curr_data->cur].count = tracing_data->count;
            curr_data->data[curr_data->cur].stage_id = global_stage_id;
            curr_data->data[curr_data->cur++].end = getMicrosecondTimestamp();
        }
        tracing_data->count++;
    }
    pthread_mutex_unlock(&mutex);
}

static PyObject *gcCallbackWrapper(PyObject *self, PyObject *args,
                                   PyObject *kwargs)
{
    PyObject *phase, *info;
    if (!PyArg_ParseTuple(args, "OO", &phase, &info))
    {
        return NULL;
    }
    gcCallback(phase, info);
    Py_RETURN_NONE;
}

static PyTypeObject GcCallbackType = {
    PyVarObject_HEAD_INIT(NULL, 0) "gc_callback", /* tp_name */
    sizeof(PyObject),                             /* tp_basicsize */
    0,                                            /* tp_itemsize */
    0,                                            /* tp_dealloc */
    0,                                            /* tp_vectorcall_offset */
    0,                                            /* tp_getattr */
    0,                                            /* tp_setattr */
    0,                                            /* tp_as_async */
    0,                                            /* tp_repr */
    0,                                            /* tp_as_number */
    0,                                            /* tp_as_sequence */
    0,                                            /* tp_as_mapping */
    0,                                            /* tp_hash  */
    gcCallbackWrapper,                            /* tp_call */
    0,                                            /* tp_str */
    0,                                            /* tp_getattro */
    0,                                            /* tp_setattro */
    0,                                            /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                           /* tp_flags */
    0,                                            /* tp_doc */
    0,                                            /* tp_traverse */
    0,                                            /* tp_clear */
    0,                                            /* tp_richcompare */
    0,                                            /* tp_weaklistoffset */
    0,                                            /* tp_iter */
    0,                                            /* tp_iternext */
    0,                                            /* tp_methods */
    0,                                            /* tp_members */
    0,                                            /* tp_getset */
    0,                                            /* tp_base */
    0,                                            /* tp_dict */
    0,                                            /* tp_descr_get */
    0,                                            /* tp_descr_set */
    0,                                            /* tp_dictoffset */
    0,                                            /* tp_init */
    0,                                            /* tp_alloc */
    0,                                            /* tp_new */
};

PyTorchTracingDataArray *
systrace_get_partial_pytorch_tracing_data_array(int name)
{
    pthread_mutex_lock(&mutex);
    TracingData *tracing_data = getTracingData(name);
    if ((!tracing_data || !tracing_data->curr_data) ||
        (tracing_data->curr_data->cur == 0))
    {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }
    PyTorchTracingDataArray *result = tracing_data->curr_data;
    tracing_data->curr_data =
        systrace_get_empty_pytorch_tracing_data_array(name);
    pthread_mutex_unlock(&mutex);
    return result;
}

void systrace_register_gc(char **error_message)
{
    addTracingData(PY_TRACING_GC, "GC");
    PyObject *gc_module = PyImport_ImportModule("gc");
    if (!gc_module)
    {
        return;
    }

    PyObject *callbacks_list = PyObject_GetAttrString(gc_module, "callbacks");
    if (!callbacks_list || !PyList_Check(callbacks_list))
    {
        Py_XDECREF(callbacks_list);
        Py_DECREF(gc_module);
        return;
    }

    PyObject *py_callback = PyObject_New(PyObject, &GcCallbackType);

    if (!py_callback)
    {
        Py_DECREF(callbacks_list);
        Py_DECREF(gc_module);
        return;
    }

    if (PyList_Append(callbacks_list, py_callback) != 0)
    {
        Py_DECREF(py_callback);
        Py_DECREF(callbacks_list);
        Py_DECREF(gc_module);
        return;
    }

    Py_DECREF(callbacks_list);
    Py_DECREF(gc_module);
    *error_message = strdup("Import gc Ok");
}

void systrace_register_tracing(const char **names, int count, char **errors)
{
    if (!Py_IsInitialized())
    {
        Py_Initialize();
    }
    PyGILState_STATE gstate = PyGILState_Ensure();
    tracing_data_count = count;
    pytorch_tracing_data_array =
        (TracingData *)malloc(sizeof(TracingData) * tracing_data_count);
    memset(pytorch_tracing_data_array, 0,
           sizeof(TracingData) * tracing_data_count);
    systrace_register_gc(errors);
    int64_t code_address;
    int is_native;

    for (int i = 1; i < count; i++)
    {
        int ret = runPyTorchCodeGetAddress(names[i], errors + i, &code_address,
                                           &is_native);
        if (ret)
        {
            printf("register function `%s` error\n", names[i]);
            continue;
        }
        printf("register function `%s` at address %ld\n", names[i],
               code_address);
        addTracingData(i, names[i]);

        TracingFunction *traced_function =
            (TracingFunction *)malloc(sizeof(TracingFunction));
        traced_function->tag_name = i;
        traced_function->function_name = strdup(names[i]);
        traced_function->py_code_address = code_address;
        traced_function->is_native = is_native;

        HASH_ADD(hh, pytorch_tracing_func_map, py_code_address, sizeof(int64_t),
                 traced_function);
    }
    PyEval_SetProfile(profiler, NULL);
    PyThreadState *tstate = PyThreadState_Get();
    PyThreadState *thread_array[PY_TRACING_MAX_THREADS];
    memset(thread_array, 0, sizeof(thread_array));
    int thread_count = 0;
    while (tstate != NULL && thread_count < PY_TRACING_MAX_THREADS)
    {
        thread_array[thread_count++] = tstate;
        printf("Set profiler for thread %ld\n", tstate->thread_id);
        tstate = PyThreadState_Next(tstate);
    }
    for (int i = 0; i < thread_count; i++)
    {
        PyThreadState_Swap(thread_array[i]);
        PyEval_SetProfile(profiler, NULL);
    }
    PyThreadState_Swap(thread_array[0]);

    PyGILState_Release(gstate);
}