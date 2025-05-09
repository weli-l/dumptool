#include "pytorch_tracing_manager.h"
#include "pytorch_tracing_data.h"
#include <cstring>
#include <thread>

namespace systrace
{
namespace pytorch_tracing_manager
{

PyTorchTracingManager &PyTorchTracingManager::getInstance()
{
    std::call_once(init_flag_, &PyTorchTracingManager::initSingleton);
    return *instance_;
}

void PyTorchTracingManager::initSingleton()
{
    instance_ = new PyTorchTracingManager();
}

PyTorchTracingDataArray *
PyTorchTracingManager::getEmptyPyTorchTracingDataArray(int name)
{
    auto &item = pool_[name];
    PyTorchTracingDataArray *data = item.empty_pool.getObject();
    std::memset(data, 0, sizeof(PyTorchTracingDataArray));
    return data;
}
void PyTorchTracingManager::returnPyTorchTracingDataArray(
    PyTorchTracingDataArray *array, int type, int name)
{
    if (!array)
        return;

    int pool_queue_size;
    auto &item = pool_[name];
    if (type == PY_TRACING_READY_POOL)
        item.ready_pool.returnObject(array, &pool_queue_size);
    else if (type == PY_TRACING_EMPTY_POOL)
        item.empty_pool.returnObject(array, &pool_queue_size);
}

PyTorchTracingDataArray *
PyTorchTracingManager::getPyTorchTracingDataArray(int name)
{
    auto &item = pool_[name];
    return item.ready_pool.getObject<false>();
}
} // namespace pytorch_tracing_manager
} // namespace systrace