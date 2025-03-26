#ifndef SINGLETON_HPP
#define SINGLETON_HPP

class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static Singleton& getInstance();

    void printMessage();

private:
    Singleton() = default;
};

#endif