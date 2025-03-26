#include "tracemanage.hpp"
#include <string>

int main() {
    TraceManage& s1 = TraceManage::getInstance();
    s1.log("First access");

    TraceManage& s2 = TraceManage::getInstance();
    s2.log("Second access");

    return 0;
}