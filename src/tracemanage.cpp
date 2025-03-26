#include "tracemanage.hpp"
#include <iostream>

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