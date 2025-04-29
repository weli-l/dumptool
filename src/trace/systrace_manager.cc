#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/process/extend.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <signal.h>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <Python.h>
#include <string>

#include "../../include/common/constant.h"
#include "systrace_manager.h"

namespace bip = boost::interprocess;
namespace systrace
{

PyTorchTrace &PyTorchTrace::getInstance()
{
    std::call_once(init_flag_, &PyTorchTrace::initSingleton);
    return *instance_;
}

void PyTorchTrace::initSingleton()
{
    instance_ = new PyTorchTrace;

    // Initialize rank and library
    instance_->pytorch_trace_.set_rank(config::GlobalConfig::rank);
    STLOG(INFO) << "[PyTorchTrace] Rank set to: " << config::GlobalConfig::rank
                << std::endl;

    instance_->pytorch_tracing_library_ =
        new pytorch_tracing::PyTorchTracingLibrary("libsysTrace.so");
    STLOG(INFO) << "[PyTorchTrace] Tracing library loaded" << std::endl;

    // Register tracing functions
    instance_->pytorch_tracing_functions_ = {
        "GC",
        "torch.utils.data.dataloader@_BaseDataLoaderIter@__next__",
        "torch_npu@npu@synchronize",
        "torch_npu.npu@Event@synchronize",
        "torch_npu.npu@Event@wait",
        "torch_npu.npu@Stream@synchronize",
        "torch_npu.npu@Stream@wait_event",
        "torch_npu.npu@Stream@wait_stream",
        "torch@autograd@backward",
        "torch@autograd@grad",
        "megatron.core.pipeline_parallel@schedules@forward_step",
        "megatron.core.pipeline_parallel@schedules@backward_step"};

    STLOG(INFO) << "[PyTorchTrace] hooked functions" << std::endl;
    std::vector<std::string> errors =
        instance_->pytorch_tracing_library_->Register(
            instance_->pytorch_tracing_functions_);
    STLOG(INFO) << "[PyTorchTrace] regits" << std::endl;
    for (size_t i = 0; i < instance_->pytorch_tracing_functions_.size(); i++)
    {
        STLOG(INFO) << "Regsiter host function "
                    << instance_->pytorch_tracing_functions_[i] << ",status "
                    << errors[i];
    }
    std::atexit([] { delete instance_; });
}
bool PyTorchTrace::triggerTrace() { return true; }

void PyTorchTrace::reset(const std::string &barrier_name)
{
    std::lock_guard<std::mutex> lock(trace_mutex_);
    STLOG(INFO) << "[PyTorchTrace] Resetting trace state at barrier: "
                << barrier_name << std::endl;

    has_trigger_trace_.store(false);
    pytorch_tracing_library_->SwitchTracing(0);

    STLOG(INFO) << "[PyTorchTrace] Reset complete" << std::endl;
}

std::string get_task_name() {
    Py_Initialize();  // 确保 Python 已初始化
    PyGILState_STATE gstate = PyGILState_Ensure();  // 获取 GIL

    std::string task_name = "default_task";
    PyObject* main_module = PyImport_AddModule("__main__");
    if (main_module) {
        PyObject* file_attr = PyObject_GetAttrString(main_module, "__file__");
        if (file_attr) {
            if (PyUnicode_Check(file_attr)) {
                const char* file_path = PyUnicode_AsUTF8(file_attr);
                if (file_path) {
                    std::string full_path(file_path);
                    size_t last_slash = full_path.find_last_of("/\\");
                    size_t last_dot = full_path.find_last_of('.');
                    if (last_slash != std::string::npos && last_dot != std::string::npos && last_dot > last_slash) {
                        task_name = full_path.substr(last_slash + 1, last_dot - last_slash - 1);
                    }
                }
            }
            Py_XDECREF(file_attr);
        } else {
            task_name = "interactive_session";  // __file__ 不存在
        }
    }

    PyGILState_Release(gstate);  // 释放 GIL
    return task_name;
}

void PyTorchTrace::dumpPyTorchTracing()
{
    const std::string &dump_path = "/root";

    if (util::ensureDirExists(dump_path))
    {
        STLOG(ERROR) << "[PyTorchTrace] Failed to create dump directory"
                     << std::endl;
        return;
    }

    std::string task_name = get_task_name();

    pytorch_trace_.set_rank(config::GlobalConfig::local_rank);
    pytorch_trace_.set_comm(task_name);

    for (size_t name_index = 0; name_index < pytorch_tracing_functions_.size();
         name_index++)
    {
        std::vector<PyTorchTracingDataArray *> holders;
        const std::string &name = pytorch_tracing_functions_[name_index];

        PyTorchTracingDataArray *tracing_data =
            pytorch_tracing_library_->GetPartialTracingData(name_index);
        if (tracing_data)
            holders.push_back(tracing_data);

        int full_data_count = 0;
        while (true)
        {
            PyTorchTracingDataArray *tracing_data =
                pytorch_tracing_library_->GetFullTracingData(name_index);
            if (!tracing_data)
                break;
            full_data_count++;
            holders.push_back(tracing_data);
        }

        for (auto each_tracing_data : holders)
        {
            for (uint32_t i = 0; i < each_tracing_data->cur; i++)
            {
                if (each_tracing_data->data[i].start == 0)
                    continue;

                auto trace = pytorch_trace_.add_pytorch_stages();
                trace->set_start_us(each_tracing_data->data[i].start);
                trace->set_end_us(each_tracing_data->data[i].end);
                trace->set_stage_id(each_tracing_data->data[i].count);
                trace->set_stage_type(name);

                if (each_tracing_data->data[i].stack_depth > 0)
                {
                    trace->mutable_stack_frames()->Reserve(
                        each_tracing_data->data[i].stack_depth);

                    for (int j = 0; j < each_tracing_data->data[i].stack_depth;
                         j++)
                    {
                        if (each_tracing_data->data[i].stack_info[j][0] != '\0')
                        {
                            trace->add_stack_frames(
                                each_tracing_data->data[i].stack_info[j]);
                        }
                    }
                }

                if (each_tracing_data->data[i].type == PAYLOAD_GC)
                {
                    GcDebugData *gc_debug = trace->mutable_gc_debug();
                    gc_debug->set_collected(
                        each_tracing_data->data[i].payload.gc_debug[0]);
                    gc_debug->set_uncollectable(
                        each_tracing_data->data[i].payload.gc_debug[1]);
                }
            }
        }

        for (auto each_tracing_data : holders)
        {
            pytorch_tracing_library_->ReturnTracingData(
                each_tracing_data, PY_TRACING_EMPTY_POOL, name_index);
        }
    }

    std::string file_path =
        dump_path + "/" + util::getUniqueFileNameByCluster(".timeline");
    std::ofstream file(file_path, std::ios::binary | std::ios::out);
    if (!file)
    {
        STLOG(ERROR) << "[PyTorchTrace] Failed to open timeline file"
                     << std::endl;
        return;
    }

    std::string binary_message;
    if (!pytorch_trace_.SerializeToString(&binary_message))
    {
        STLOG(ERROR) << "[PyTorchTrace] Failed to serialize timeline"
                     << std::endl;
        return;
    }

    file << binary_message;
}

SysTrace &SysTrace::getInstance()
{
    STLOG(INFO) << "[SysTrace] Getting instance" << std::endl;

    SysTrace *instance = instance_.load(std::memory_order_acquire);
    if (!instance)
    {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        instance = instance_.load(std::memory_order_relaxed);
        MSPTITracker::getInstance();
        if (!instance)
        {
            instance = new SysTrace();
            instance_.store(instance, std::memory_order_release);

            instance->startWork();

            std::atexit(
                []
                {
                    STLOG(INFO)
                        << "[SysTrace] Cleaning up instance" << std::endl;
                    delete instance_.load();
                });
        }
    }

    return *instance;
}

void SysTrace::stopWork() noexcept
{
    if (!config::GlobalConfig::enable)
    {
        return;
    }
    should_run_.store(false);
    if (event_poller_.joinable())
    {
        event_poller_.join();
    }
}

void SysTrace::doWork()
{
    while (should_run_.load())
    {
        if (loop_count_.fetch_add(1) %
                constant::TorchTraceConstant::DEFAULT_TRACE_COUNT ==
            0)
        {
            if (PyTorchTrace::getInstance().triggerTrace())
            {
                PyTorchTrace::getInstance().dumpPyTorchTracing();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (PyTorchTrace::getInstance().triggerTrace())
    {
        PyTorchTrace::getInstance().dumpPyTorchTracing();
    }
}

void SysTrace::startWork()
{

    config::setUpConfig();

    if (!config::GlobalConfig::enable)
    {
        return;
    }

    setLoggingPath();

    PyTorchTrace::getInstance();

    should_run_.store(true);

#ifdef _GNU_SOURCE
    event_poller_ = std::thread(&SysTrace::doWork, this);
    auto handle = event_poller_.native_handle();
    pthread_setname_np(handle, "systrace_poller");
#endif

    STLOG(INFO) << "[SysTrace] Work started" << std::endl;
}
} // namespace systrace