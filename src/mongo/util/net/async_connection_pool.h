/*
 * async_connection_pool.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/util/net/client_async_message_port.h"

namespace mongo {
namespace network {

/*
 * TODO: NUMA aware handling will be added one day, so NONE of this is static
 * All functions starting with async are calling from async functions, should not
 * take locks if at all possible
 */
//TODO: MONGO_ALIGN_TO_CACHE
//TODO: Init function to create 1000 idle ports, maybe based on shards?
class AsyncConnectionPool {
public:
    MONGO_DISALLOW_COPYING(AsyncConnectionPool);
    //TODO: Remove std::function and replace with direct calls, type erase is expensive
    using MessageReadyHandler = std::function<void(AsyncMessagePort*)>;
    AsyncConnectionPool(AsioAsyncServer* const server, MessageReadyHandler messageReadyHandler) :
            _server(server), _messageReadyHandler(messageReadyHandler) {
    }
    ~AsyncConnectionPool();

    void newConnHandler(asio::ip::tcp::socket&& socket);
    // The port is connected starting
    void handlerPortActive(AsyncMessagePort* port) {
        _activeConns.insert(port);
    }
    // Passing message, which shouldn't allocate any buffers
    void handlerOperationReady(AsyncMessagePort* port);
    // The port is closed and should accept no more operations
    void handlerPortClosed(AsyncMessagePort* port);

    bool getCachedConn(AsyncMessagePort** port) {
        return _freeConns.pop(port);
    }
    const ConnStats& getStats() const {
        return _stats;
    }


private:
    using FreeQueue = ThreadSafeQueue<AsyncMessagePort*>;

    AsioAsyncServer* const _server;
    MessageReadyHandler _messageReadyHandler;
    UnboundedContainer<AsyncMessagePort*> _activeConns;
    FreeQueue _freeConns;
    ConnStats _stats;
};

} /* namespace network */
} /* namespace mongo */

