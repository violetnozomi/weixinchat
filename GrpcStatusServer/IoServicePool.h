//
// Created by EDPerson on 2025/12/31.
//
#include <thread>
#include <vector>
#include "boost/asio.hpp"

#include "Singleton.h"
#ifndef SOCKETPROJECT_ASIOIOSERVICEPOOL_H
#define SOCKETPROJECT_ASIOIOSERVICEPOOL_H
class Session;

class AsioioservicePool:public Singleton<AsioioservicePool>{
    friend Singleton<AsioioservicePool>;
public:

    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    boost::asio::io_context& Get_iocontext();
    void stop();
    ~AsioioservicePool();
    AsioioservicePool(const AsioioservicePool&) = delete;
    AsioioservicePool& operator=(const AsioioservicePool&) = delete;
private:
    AsioioservicePool(std::size_t size = std::thread::hardware_concurrency());
    std::vector<boost::asio::io_context> _IOContexts;
    std::vector<std::thread> _Threads;
    std::atomic<std::size_t> _next_io_context;  // 原子操作保证线程安全
    std::vector<std::unique_ptr<Work>>  _works;



};


#endif //SOCKETPROJECT_ASIOIOSERVICEPOOL_H