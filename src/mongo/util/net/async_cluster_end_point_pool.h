/*
 * async_cluster_connection_pool.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include <asio.hpp>
#include <system_error>
#include <memory>
#include <thread>
#include <unordered_map>

#include "mongo/util/concurrency/queue.h"
#include "mongo/util/concurrency/rwlock.h"
#include "mongo/util/concurrency/thread_pool_interface.h"
#include "mongo/util/net/async_cluster_connection.h"

namespace mongo {
namespace network {

// TODO: The pool and server have been rolled into one, should probably unroll
// TODO: align and pad everything
class AsyncClusterEndPointPool {
public:
    MONGO_DISALLOW_COPYING(AsyncClusterEndPointPool);
    using ClusterConnPtr = std::unique_ptr<AsyncClusterConnection>;

    AsyncClusterEndPointPool(const size_t threads);
    virtual ~AsyncClusterEndPointPool();

    using Callback = std::function<void(std::error_code& ec, void* key, void* returnData,
            size_t returnSize)>;
    /*
     * Data to send over the wire, this assumes the caller survives the lifetime
     */
    // TODO: Replace this with some templates or abstract class and get rid of std::function,
    // just wanted one variable to pass for the time being
    struct asyncData {
    public:
        asyncData(Callback callback, void* data, size_t size) : _callback(callback), _data(data),
                _size(size) { }

        // Performs the call back, data() is used as the key
        void callback(std::error_code& ec, void* data__, size_t size__) const {
            _callback(ec, data(), data__, size__); }
        void* data() const { return _data; }
        size_t size() const { return _size; }

    private:
        const Callback _callback;
        // data is returned as the key
        void* const _data;
        const size_t _size;
    };

    void asyncSendData(const ConnectionString& endPoint, const asyncData& sendData);
    ClusterConnPtr createConnection(const ConnectionString& conn);

private:
    using FreeQueue = ThreadSafeQueue<ClusterConnPtr>;

    void startThreads(size_t threads);

    class ServerConnPool {
    public:
        ServerConnPool(const ConnectionString& conn);
        MONGO_DISALLOW_COPYING(ServerConnPool);

        AsyncClusterEndPointPool* _owner;
        asio::ip::tcp::resolver _resolver;
        FreeQueue _queue;
    };

    //The string here is the ConnectionString.toString()
    using PoolMap = std::unordered_map<std::string, ServerConnPool>;
    // TODO: Make this more efficient
    void createPool(const ConnectionString& endPoint, const asyncData& sendData);
    void createNewConn(std::string&& endPointKey, const asyncData& sendData);

    RWLock _lock;
    PoolMap _pools;

    asio::io_service _ioService;
    std::unique_ptr<ThreadPoolInterface> _threadPool;
};

} /* namespace network */
} /* namespace mongo */
