/*
 * async_cluster_connection_pool.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "mongo/util/net/async_cluster_end_point_pool.h"

#include "mongo/util/concurrency/thread_pool.h"

namespace mongo {
namespace network {

AsyncClusterEndPointPool::AsyncClusterEndPointPool(const size_t threads) :
    _lock("AsyncClusterEndPointPool") {
    startThreads(threads);
}

AsyncClusterEndPointPool::~AsyncClusterEndPointPool() {
    _ioService.stop();
    for (auto& t : _threads)
        t.join();
}

void AsyncClusterEndPointPool::startThreads(size_t threads) {
    for (; threads > 0; --threads)
        _threads.emplace_back([this] {serviceRun();});
}

void AsyncClusterEndPointPool::serviceRun() {
    try {
        asio::error_code ec;
        asio::io_service::work work(_ioService);
        _ioService.run(ec);
        if (ec) {
            log() << "Error running service: " << ec << std::endl;
        }
    } catch (std::exception& e) {
        log() << "Exception running io_service: " << e.what() << std::endl;
        throw e;
    } catch (...) {
        log() << "unknown error running io_service" << std::endl;
        throw;
    }
}


void AsyncClusterEndPointPool::asyncSendData(const ConnectionString& endPoint,
        const AsyncData& sendData) {
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
        return createNewConn(&pool->second, sendData);
    }
}

//TODO: Make this more efficient
void AsyncClusterEndPointPool::createPool(const ConnectionString& endPoint,
        const AsyncData& sendData) {
    std::string endPointKey = endPoint.toString();
    // TODO: Guard
    _lock.lock();
    auto pool = _pools.find(endPointKey);
    // If another thread already inserted while we waited, start the send
    if (pool == _pools.end()) {
        _pools.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(endPointKey)),
                std::forward_as_tuple(endPoint, &_ioService));
    }
    _lock.unlock();
    asyncSendData(endPoint, sendData);
}

void AsyncClusterEndPointPool::createNewConn(ServerConnPool* pool,
        const AsyncData& sendData) {

    //ClusterConnPtr ptr = stdx::make_unique<ClusterConnPtr>()

    asio::ip::tcp::resolver::query query(pool->_hostName, pool->_hostPort);
    pool->_resolver.async_resolve(query, [this, pool, sendData] (
            const std::error_code& ec,
                  asio::ip::tcp::resolver::iterator endPoints) {
        if (ec) {
            // TODO: Is this the right thing to do?  Try again?
            return const_cast<AsyncData*>(&sendData)->callback(false, nullptr, 0);
        }
        AsyncClusterConnection* newConn(new AsyncClusterConnection(pool, &_ioService));
        newConn->connectAndSend(std::move(endPoints), std::move(sendData));
    });
}

ServerConnPool::ServerConnPool(const ConnectionString& conn,
        asio::io_service* ioService) : _resolver (*ioService) {
    //This pool only supports single servers, this string should only have that
    auto vHostAndPort = conn.getServers();
    verify(vHostAndPort.size() == 1);
    _hostName = vHostAndPort.front().host();
    _hostPort = vHostAndPort.front().port();
}

void ServerConnPool::handlerConnFree(AsyncClusterConnection* freeConn) {
    _queue.emplace(freeConn);
}

} /* namespace network */
} /* namespace mongo */
