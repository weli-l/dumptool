#include "manage.hpp"
#include <iostream>

Singleton& Singleton::getInstance() {
    static Singleton instance;
    return instance;
}

void Singleton::printMessage() {
    std::cout << "Singleton instance address: " << this << std::endl;
}