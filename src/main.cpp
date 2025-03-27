#include "tracemanage.hpp"
#include <string>

int main() {
    sys_trace::TraceManage& s1 = sys_trace::TraceManage::getInstance();
    s1.log("First access");
    s1.Start();

    return 0;
}