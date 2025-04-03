#include <iostream>
#include "src/trace/systrace_manager.h"
using namespace systrace;
int main(){
    PyTorchTrace& trace = PyTorchTrace::getInstance();
    std::cout << "Trace instance created" << std::endl;
    return 0;
}