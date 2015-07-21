/*
 * async_cluster_connection_pool.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include <asio.hpp>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mongo/util/concurrency/queue.h"
#include "mongo/util/concurrency/rwlock.h"
#include "mongo/util/concurrency/thread_pool_interface.h"
#include "mongo/util/net/async_cluster_connection.h"
#include "mongo/util/net/async_data.h"

namespace mongo {
namespace network {

using ClusterConnPtr = std::unique_ptr<AsyncClusterConnection>;

struct ServerConnPool {
public:
    MONGO_DISALLOW_COPYING(ServerConnPool);
    using FreeQueue = ThreadSafeQueue<ClusterConnPtr>;

    ServerConnPool(const ConnectionString& conn, asio::io_service* ioService);
    void handlerConnFree(AsyncClusterConnection* freeConn);

    // TODO: Make resolve more concurrent
    asio::ip::tcp::resolver _resolver;
    std::string _hostName;
    std::string _hostPort;
    FreeQueue _queue;
};


// TODO: The pool and server have been rolled into one, should probably unroll
// TODO: align and pad everything
class AsyncClusterEndPointPool {
public:
    MONGO_DISALLOW_COPYING(AsyncClusterEndPointPool);

    AsyncClusterEndPointPool(const size_t threads);
    virtual ~AsyncClusterEndPointPool();

    void asyncSendData(const ConnectionString& endPoint, const AsyncData& sendData);
    ClusterConnPtr createConnection(const ConnectionString& conn);

private:
    friend AsyncClusterConnection;


    void startThreads(size_t threads);
    void serviceRun();

    //The string here is the ConnectionString.toString()
    using PoolMap = std::unordered_map<std::string, ServerConnPool>;
    // TODO: Make this more efficient
    void createPool(const ConnectionString& endPoint, const AsyncData& sendData);
    void createNewConn(ServerConnPool* pool, const AsyncData& sendData);

    RWLock _lock;
    PoolMap _pools;

    asio::io_service _ioService;
    std::vector<std::thread> _threads;
};

} /* namespace network */
} /* namespace mongo */
