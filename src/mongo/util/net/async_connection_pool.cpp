/*
 * async_connection_pool.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "../../util/net/async_connection_pool.h"

namespace mongo {
namespace network {

void AsyncConnectionPool::handlerOperationReady(AsyncMessagePort* conn) {
    _messageReadyHandler(conn);
}

void AsyncConnectionPool::handlerPortClosed(AsyncMessagePort* port) {
    _activeConns.erase(port);
    _freeConns.emplace(std::move(port));
}

} /* namespace network */
} /* namespace mongo */
