#include "manage.hpp"
#include <iostream>

int main(int argc, char** argv) {
    Singleton& instance = Singleton::getInstance();
    instance.printMessage();

    Singleton& sameInstance = Singleton::getInstance();
    sameInstance.printMessage();

    return 0;
}