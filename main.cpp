#include <iostream>
#include "src/trace/systrace_manager.h"
using namespace systrace;
int main(){
    SysTrace::getInstance();
    std::cout << "Trace instance created" << std::endl;
    return 0;
}