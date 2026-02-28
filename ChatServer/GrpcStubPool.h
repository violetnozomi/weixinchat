#pragma once
// ────────────────────────────────────────────────────────────────────────────
// GrpcStubPool<StubT>  +  GrpcStubGuard<StubT>
//
// 通用 gRPC Stub 连接池，支持任意 gRPC 服务。
//
// 用法示例：
//   auto pool = std::make_unique<GrpcStubPool<StatusService::Stub>>(
//       5, "127.0.0.1:50052",
//       [](auto ch){ return StatusService::NewStub(ch); });
//
//   {
//       GrpcStubGuard<StatusService::Stub> g = pool->acquire();
//       g->LoginVerify(&ctx, req, &rsp);
//   }  // 离开作用域时自动归还连接池
// ────────────────────────────────────────────────────────────────────────────

#include <grpcpp/grpcpp.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <iostream>

// 前向声明，让 GrpcStubGuard 知道 GrpcStubPool 的存在
template<typename StubT>
class GrpcStubPool;

// ────────────────────────────────────────────────────────────────────────────
// GrpcStubGuard<StubT>
//
// RAII 持有一个从连接池借出的 Stub。
// 离开作用域时自动调用 pool.returnStub()，将连接归还到池中。
// 只能移动，不能拷贝（保证一个 Stub 同时只被一个 Guard 持有）。
// ────────────────────────────────────────────────────────────────────────────
template<typename StubT>
class GrpcStubGuard {
public:
    using StubPtr = std::shared_ptr<StubT>;

    // 空 Guard（连接池关闭 / 获取失败时返回）
    GrpcStubGuard() = default;

    // 由连接池内部构造，外部不直接调用
    GrpcStubGuard(GrpcStubPool<StubT>* pool, StubPtr stub)
        : _pool(pool), _stub(std::move(stub)) {}

    // 不允许拷贝
    GrpcStubGuard(const GrpcStubGuard&)            = delete;
    GrpcStubGuard& operator=(const GrpcStubGuard&) = delete;

    // 允许移动
    GrpcStubGuard(GrpcStubGuard&& other) noexcept
        : _pool(other._pool), _stub(std::move(other._stub))
    {
        other._pool = nullptr;
    }

    GrpcStubGuard& operator=(GrpcStubGuard&& other) noexcept {
        if (this != &other) {
            returnToPool();           // 先归还自己持有的
            _pool        = other._pool;
            _stub        = std::move(other._stub);
            other._pool  = nullptr;
        }
        return *this;
    }

    // 析构时自动归还
    ~GrpcStubGuard() {
        returnToPool();
    }

    // 像指针一样调用 gRPC 方法：guard->LoginVerify(...)
    StubT* operator->() const { return _stub.get(); }

    // 检查是否持有有效连接（连接池关闭时返回 false）
    bool valid() const { return _stub != nullptr; }

private:
    // 将 Stub 归还给原连接池
    void returnToPool() {
        if (_pool && _stub) {
            _pool->returnStub(std::move(_stub));
            _pool = nullptr;
        }
    }

    GrpcStubPool<StubT>* _pool{ nullptr };
    StubPtr              _stub;
};

// ────────────────────────────────────────────────────────────────────────────
// GrpcStubPool<StubT>
//
// 维护一组预建的 gRPC Stub，线程安全地借出 / 归还。
// ────────────────────────────────────────────────────────────────────────────
template<typename StubT>
class GrpcStubPool {
public:
    using StubPtr = std::shared_ptr<StubT>;
    // 工厂函数：接受 Channel，返回对应 Stub
    using Factory = std::function<StubPtr(std::shared_ptr<grpc::Channel>)>;

    // 构造：预建 size 个连接
    GrpcStubPool(size_t size, const std::string& address, Factory factory)
        : _size(size), _address(address), _factory(factory), _stop(false)
    {
        for (size_t i = 0; i < size; ++i) {
            auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
            _stubs.push(_factory(std::move(channel)));
        }
        std::cout << "[GrpcStubPool] " << address
                  << "  pool=" << size << " stubs ready" << std::endl;
    }

    ~GrpcStubPool() { close(); }

    // 关闭连接池，唤醒所有等待的线程
    void close() {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _stop = true;
        }
        _cv.notify_all();
    }

    // 借出一个 Stub（阻塞等待，直到有空闲连接或连接池被关闭）
    // 返回的 GrpcStubGuard 离开作用域后自动归还
    GrpcStubGuard<StubT> acquire() {
        std::unique_lock<std::mutex> lk(_mutex);
        _cv.wait(lk, [this] { return _stop || !_stubs.empty(); });

        if (_stop || _stubs.empty()) {
            return GrpcStubGuard<StubT>{};   // 无效 Guard
        }

        auto stub = std::move(_stubs.front());
        _stubs.pop();
        return GrpcStubGuard<StubT>{ this, std::move(stub) };
    }

    // 当前空闲连接数
    size_t available() {
        std::lock_guard<std::mutex> lk(_mutex);
        return _stubs.size();
    }

    const std::string& address() const { return _address; }

    // 由 GrpcStubGuard 的析构函数调用，外部不需要直接调用
    void returnStub(StubPtr stub) {
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (!_stop) {
                _stubs.push(std::move(stub));
                should_notify = true;
            }
        }
        if (should_notify) {
            _cv.notify_one();
        }
    }

private:
    size_t                  _size;
    std::string             _address;
    Factory                 _factory;
    bool                    _stop;
    std::queue<StubPtr>     _stubs;
    std::mutex              _mutex;
    std::condition_variable _cv;
};


