//
// Created by EDPerson on 2026/2/13.
//

#ifndef CHATSERVER_SINGLETON_H
#define CHATSERVER_SINGLETON_H

#include <iostream>
#include "const.h"

// Meyers Singleton
//
// 原理：C++11 保证局部 static 变量的初始化是线程安全的（"magic statics"）。
// 对象存放在静态存储区，析构顺序严格按照初始化的逆序——
// 若 A 的构造触发了 B::GetInstance()，则 B 先于 A 初始化，
// 因此 B 晚于 A 析构，跨单例析构依赖问题自动消失，无需手动管理顺序。
//
// 返回裸指针（非拥有语义）：调用方不持有所有权，对象生命周期由 static 管理。
template <typename T>
class Singleton {
protected:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

public:
    static T* GetInstance() {
        static T instance;   // 第一次调用时初始化，线程安全；之后直接返回
        return &instance;
    }

    void PrintAddress() {
        std::cout << this << std::endl;
    }

protected:
    // 析构设为 protected：防止外部代码通过 Singleton<T>* 指针 delete 对象。
    // 子类的析构函数可以正常调用（正常析构链）。
    ~Singleton() = default;
};

#endif //CHATSERVER_SINGLETON_H
