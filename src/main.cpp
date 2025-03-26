#include "tracemanage.hpp"
#include <string>

int main() {
    TraceManage& s1 = TraceManage::getInstance();
    s1.log("First access");
    s1.Start();

    return 0;
}