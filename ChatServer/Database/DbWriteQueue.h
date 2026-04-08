// ChatServer/DbWriteQueue.h
// 专用数据库写操作队列：逻辑线程（生产者）投递任务，独立工作线程（消费者）异步执行。
// 只用于 fire-and-forget 的 INSERT / UPDATE / DELETE，读操作不经过此队列。
//
#ifndef CHATSERVER_DBWRITEQUEUE_H
#define CHATSERVER_DBWRITEQUEUE_H

#include "Singleton.h"
#include <functional>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>

class DbWriteQueue : public Singleton<DbWriteQueue> {
    friend class Singleton<DbWriteQueue>;

public:
    using WriteTask = std::function<void()>;

    // 任意线程调用：将写任务入队后立即返回，不阻塞调用方
    void PostWrite(WriteTask task);

    // 优雅停机：等待所有 worker 处理完队列中的剩余任务后返回
    void Stop();

    // 当前 worker 线程数（供外部日志打印）
    size_t WorkerCount() const { return _workers.size(); }

    ~DbWriteQueue();

private:
    DbWriteQueue();
    void ProcessLoop();   // 每个 worker 线程执行此循环

    std::deque<WriteTask>         _queue;
    std::mutex                    _mutex;
    std::condition_variable       _cv;
    std::atomic<bool>             _stopped{false};
    std::vector<std::thread>      _workers;  // 多 worker 线程池
};

#endif // CHATSERVER_DBWRITEQUEUE_H
