// ChatServer/DbWriteQueue.cpp
//
#include "DbWriteQueue.h"
#include <iostream>
#include <thread>
#include <algorithm>

DbWriteQueue::DbWriteQueue() {
    // 写操作（INSERT/UPDATE/DELETE）一般比读少，2 个 worker 足以消化积压；
    // 若 hardware_concurrency >= 8 则开 3 个，给高并发写留余量
    const size_t cnt = (std::thread::hardware_concurrency() >= 8) ? 3 : 2;
    _workers.reserve(cnt);
    for (size_t i = 0; i < cnt; ++i)
        _workers.emplace_back(&DbWriteQueue::ProcessLoop, this);
    std::cout << "[DbWriteQueue] " << cnt << " workers started" << std::endl;
}

DbWriteQueue::~DbWriteQueue() {
    Stop();
}

// ─── 生产者接口：入队后立即返回 ──────────────────────────────────────────────
void DbWriteQueue::PostWrite(WriteTask task) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(task));
    }
    // 只唤醒一个 worker，避免惊群
    _cv.notify_one();
}

// ─── 多 worker 消费循环：每次持锁取一个任务，释放锁后执行 ──────────────────
void DbWriteQueue::ProcessLoop() {
    while (true) {
        WriteTask task;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] {
                return _stopped.load() || !_queue.empty();
            });
            // 停机且队列已清空：退出（确保积压任务全部执行完）
            if (_stopped.load() && _queue.empty()) break;
            task = std::move(_queue.front());
            _queue.pop_front();
        }
        // 锁已释放，其他 worker 可并行取任务
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[DbWriteQueue] Task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[DbWriteQueue] Task unknown exception" << std::endl;
        }
    }
}

// ─── 优雅停机 ─────────────────────────────────────────────────────────────────
void DbWriteQueue::Stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopped = true;
    }
    // 唤醒所有 worker，使其检查停机条件并退出
    _cv.notify_all();
    for (auto& t : _workers)
        if (t.joinable()) t.join();
}
