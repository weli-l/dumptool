#include "tracemanage.hpp"

std::unique_ptr<TraceManage> TraceManage::instance;
std::once_flag TraceManage::init_flag;

TraceManage& TraceManage::getInstance() {
    std::call_once(init_flag, [](){
        instance = std::unique_ptr<TraceManage>(new TraceManage());
    });
    return *instance;
}

void TraceManage::log(const std::string& message) const {
    std::cout << "[TraceManage@" << this << "] " << message << std::endl;
}

void TraceManage::Start() {
    StartWork();
}

void TraceManage::Stop() {
    StopWork();
}

void TraceManage::StartWork() {
    if (!running_.exchange(true)) {
        worker_thread_ = std::thread(&TraceManage::dowork, this);
        std::cout << "Background worker started\n";
        std::this_thread::sleep_for(std::chrono::seconds(100));
    }
}

void TraceManage::StopWork() {
    if (running_.exchange(false)) {
        worker_thread_.join();
        std::cout << "Background worker stopped\n";
    }
}

void TraceManage::dowork() {
    while (running_) {
        log("Worker is running");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}