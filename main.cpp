#include "Singleton.hpp"
#include <iostream>

int main() {
    // 获取单例实例
    Singleton& instance = Singleton::getInstance();
    instance.printMessage();

    // 再次获取验证单例
    Singleton& sameInstance = Singleton::getInstance();
    sameInstance.printMessage();

    return 0;
}