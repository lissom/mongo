/*
 * async_cluster_connection_pool.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "async_cluster_end_point_pool.h"

#include "mongo/util/concurrency/thread_pool.h"

namespace mongo {
namespace network {

AsyncClusterEndPointPool::AsyncClusterEndPointPool(const size_t threads) :
    _lock("AsyncClusterEndPointPool"), _threadPool(new ThreadPool(ThreadPool::Options(
    "ClusterConnPool", "ClusterConn", threads, threads))) {
    _threadPool->startup();
    _threadPool->schedule([this, threads] {
        startThreads(threads);
    });
}

AsyncClusterEndPointPool::~AsyncClusterEndPointPool() {
    _threadPool->shutdown();
    _threadPool->join();
}

void AsyncClusterEndPointPool::startThreads(size_t threads) {
    for (; threads > 0; --threads)
        _threadPool->schedule([this] {
        //TODO: Wrap this to catch exceptions
                _ioService.run();
        });
}

void AsyncClusterEndPointPool::asyncSendData(const ConnectionString& endPoint,
        const asyncData& sendData) {
    std::string endPointKey = endPoint.toString();
    _lock.lock_shared();
    auto pool = _pools.find(endPointKey);
    bool found = pool == _pools.end();
    _lock.unlock_shared();
    if (!found) {
        return createPool(endPoint, sendData);
    }
    ClusterConnPtr conn;
    if (!pool->second._queue.pop(&conn)) {
        return createNewConn(std::move(endPointKey), sendData);
    }
}

//TODO: Make this more efficient
void AsyncClusterEndPointPool::createPool(const ConnectionString& endPoint,
        const asyncData& sendData) {
    std::string endPointKey = endPoint.toString();
    // TODO: Guard
    _lock.lock();
    auto pool = _pools.find(endPointKey);
    // If another thread already inserted while we waited, start the send
    if (pool == _pools.end()) {
        _pools.emplace(std::move(endPointKey), ServerConnPool(endPoint));
    }
    _lock.unlock();
    asyncSendData(endPoint, sendData);
}

void AsyncClusterEndPointPool::createNewConn(const ConnectionString& endPoint,
        const asyncData& sendData) {
    AsyncClusterEndPointPool::ClusterConnPtr newConn;
    if(!_queue.pop(&newConn)) {
        //TODO: Async resolve
        _resolver.resolve()
    }
    return newConn;
}

} /* namespace network */
} /* namespace mongo */
