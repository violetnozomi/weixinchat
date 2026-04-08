//
// Created by EDPerson on 2026/2/3.
//

#ifndef WXCHAT_SINGLETON_H
#define WXCHAT_SINGLETON_H

#include <iostream>
#include "const.h"

// Meyers Singleton
//
// C++11 保证局部 static 的初始化线程安全（magic statics）。
// 析构顺序是初始化的逆序，跨单例析构依赖自动安全。
template <typename T>
class Singleton {
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

public:
    static T* GetInstance() {
        static T instance;
        return &instance;
    }

    void PrintAddress() {
        std::cout << this << std::endl;
    }

protected:
    ~Singleton() = default;
};

#endif // WXCHAT_SINGLETON_H
