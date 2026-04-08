// ChatServer/DbReadQueue.h
// 专用数据库读操作队列（生产者-消费者）。
//
// 使用模式：
//   DbReadQueue::GetInstance()->PostRead(
//       [captures]() {                                     // ① DB 线程执行：SELECT，返回结果
//           auto data = ...mysql query...;
//           ChatLogicSystem::GetInstance()->PostTask(      // ② 将结果打包成续体（continuation）
//               [data, weak_session]() {                   //    投回逻辑线程
//                   auto sess = weak_session.lock();
//                   if (!sess) return;
//                   sess->Send(...);
//               });
//       });
//
// DB 线程只负责查库并将结果"邮寄"回逻辑线程；
// 网络发送始终在逻辑线程发起，对 CSession 的访问天然串行。
//
#ifndef CHATSERVER_DBREADQUEUE_H
#define CHATSERVER_DBREADQUEUE_H

#include "Singleton.h"
#include <functional>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

class DbReadQueue : public Singleton<DbReadQueue> {
    friend class Singleton<DbReadQueue>;

public:
    using ReadTask = std::function<void()>;

    // 任意线程调用：入队后立即返回，由某一空闲 worker 竞争取走执行
    void PostRead(ReadTask task);

    // 优雅停机：等待所有 worker 处理完队列中的剩余任务后返回
    void Stop();

    // 当前 worker 线程数（供外部日志打印）
    size_t WorkerCount() const { return _workers.size(); }

    ~DbReadQueue();

private:
    DbReadQueue();
    void ProcessLoop();   // 每个 worker 线程执行此循环

    std::deque<ReadTask>         _queue;
    std::mutex                   _mutex;
    std::condition_variable      _cv;
    std::atomic<bool>            _stopped{false};
    std::vector<std::thread>     _workers;  // 多 worker 线程池
};

#endif // CHATSERVER_DBREADQUEUE_H
