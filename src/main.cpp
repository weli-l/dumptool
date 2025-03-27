#include "tracemanage.hpp"
#include <string>

int main() {
    sys_trace::TraceManage& s1 = sys_trace::TraceManage::getInstance();
    s1.log("First access");
    sys_trace::DumpConfig config;
    config.path = "/home/dump.log";
    config.interval = 30;
    s1.Start(config);

    return 0;
}