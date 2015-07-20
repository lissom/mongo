/*
 * async_connection_pool.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "mongo/util/net/async_client_pool.h"

namespace mongo {
namespace network {

void AsyncClientPool::handlerOperationReady(AsyncMessagePort* conn) {
    _messageReadyHandler(conn);
}

void AsyncClientPool::handlerPortClosed(AsyncMessagePort* port) {
    _activeConns.erase(port);
    _freeConns.emplace(std::move(port));
}

} /* namespace network */
} /* namespace mongo */
