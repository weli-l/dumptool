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
    instance_->file_watcher_ = std::thread(&PyTorchTrace::watchControlFile);

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

void PyTorchTrace::dumpPyTorchTracing()
{
    if (dump_)
    {
        const std::string &dump_path = "/root";

        if (util::ensureDirExists(dump_path))
        {
            STLOG(ERROR) << "[PyTorchTrace] Failed to create dump directory"
                         << std::endl;
            return;
        }

        pytorch_trace_.set_rank(config::GlobalConfig::local_rank);
        pytorch_trace_.set_step_id(1);
        pytorch_trace_.set_comm("default_task");

        for (size_t name_index = 0;
             name_index < pytorch_tracing_functions_.size(); name_index++)
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

                    if (dump_stack_ &&
                        each_tracing_data->data[i].stack_depth > 0)
                    {
                        trace->mutable_stack_frames()->Reserve(
                            each_tracing_data->data[i].stack_depth);

                        for (int j = 0;
                             j < each_tracing_data->data[i].stack_depth; j++)
                        {
                            if (each_tracing_data->data[i].stack_info[j][0] !=
                                '\0')
                            {
                                trace->add_stack_frames(
                                    each_tracing_data->data[i].stack_info[j]);
                            }
                        }
                    }

                    if (dump_gc_ &&
                        each_tracing_data->data[i].type == PAYLOAD_GC)
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
        std::ofstream file(file_path,
                           std::ios::binary | std::ios::out | std::ios::app);
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
    if (!sampling_active_)
    {
        sampling_start_ = std::chrono::steady_clock::now();
        sampling_active_ = true;
        STLOG(INFO) << "[SysTrace] Started 3-minute sampling window";
    }

    while (should_run_.load())
    {
        if (std::chrono::steady_clock::now() - sampling_start_ >
            std::chrono::minutes(3))
        {
            if (sampling_active_)
            {
                STLOG(INFO) << "[SysTrace] 3-minute sampling window ended";
                sampling_active_ = false;
            }
            continue;
        }

        if (loop_count_.fetch_add(1) %
                constant::TorchTraceConstant::DEFAULT_TRACE_COUNT ==
            0)
        {
            if (PyTorchTrace::getInstance().triggerTrace())
            {
                PyTorchTrace::getInstance().dumpPyTorchTracing();
            }
        }
    }

    if (sampling_active_ && PyTorchTrace::getInstance().triggerTrace())
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