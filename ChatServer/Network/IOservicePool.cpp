//
// Created by EDPerson on 2026/2/13.
//

#include "IOservicePool.h"
//
// Created by EDPerson on 2025/12/31.
//

AsioioservicePool::AsioioservicePool(std::size_t size)
    :_IOContexts(size),_works(size),_next_io_context(0) {
    for (std::size_t i = 0; i < size; ++i) {
        _works[i] = std::make_unique<Work>(boost::asio::make_work_guard(_IOContexts[i]));
    }
    for (std::size_t i = 0; i < size; ++i) {
        _Threads.emplace_back([this, i]() {
            _IOContexts[i].run();
        });
    }
}

AsioioservicePool::~AsioioservicePool() {
    stop();
}
boost::asio::io_context &AsioioservicePool::Get_iocontext() {
    // 使用原子操作保证线程安全的轮询
    auto index = _next_io_context.fetch_add(1) % _IOContexts.size();
    return _IOContexts[index];
}
void AsioioservicePool::stop() {
    // 停止所有work guards，让io_context可以退出
    for (auto &work : _works) {
        if (work) {
            work->reset();
        }
    }

    // 等待所有线程完成
    for (auto &thread : _Threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}


