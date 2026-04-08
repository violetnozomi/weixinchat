// ChatServer/DbReadQueue.cpp
//
#include "DbReadQueue.h"
#include <iostream>
#include <thread>
#include <algorithm>

DbReadQueue::DbReadQueue() {
    // worker 数 = max(2, hardware_concurrency / 2)，上限 4
    // 读操作（SELECT + 续体回逻辑线程）可真正并发，多 worker 减少排队延迟
    const size_t hw  = std::thread::hardware_concurrency();
    const size_t cnt = std::min<size_t>(4, std::max<size_t>(2, hw / 2));
    _workers.reserve(cnt);
    for (size_t i = 0; i < cnt; ++i)
        _workers.emplace_back(&DbReadQueue::ProcessLoop, this);
    std::cout << "[DbReadQueue] " << cnt << " workers started" << std::endl;
}

DbReadQueue::~DbReadQueue() {
    Stop();
}

void DbReadQueue::PostRead(ReadTask task) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(task));
    }
    // 只唤醒一个 worker，避免惊群
    _cv.notify_one();
}

// 多 worker 竞争消费：每次持锁取一个任务，释放锁后执行，
// 允许其他 worker 在执行期间并行取走下一个任务。
void DbReadQueue::ProcessLoop() {
    while (true) {
        ReadTask task;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] {
                return _stopped.load() || !_queue.empty();
            });
            // 停机且队列已清空：退出
            if (_stopped.load() && _queue.empty()) break;
            task = std::move(_queue.front());
            _queue.pop_front();
        }
        // 锁已释放，其他 worker 可继续取任务
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[DbReadQueue] Task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[DbReadQueue] Task unknown exception" << std::endl;
        }
    }
}

void DbReadQueue::Stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopped = true;
    }
    // 唤醒所有 worker，使其检查 _stopped 并退出
    _cv.notify_all();
    for (auto& t : _workers)
        if (t.joinable()) t.join();
}
