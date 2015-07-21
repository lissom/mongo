/*
 * async_connection_pool.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "mongo/util/net/async_client_message_port_pool.h"

namespace mongo {
namespace network {

void AsyncClientMessagePortPool::handlerOperationReady(AsyncMessagePort* conn) {
    _messageReadyHandler(conn);
}

void AsyncClientMessagePortPool::handlerPortClosed(AsyncMessagePort* port) {
    _activeConns.erase(port);
    _freeConns.emplace(std::move(port));
}

} /* namespace network */
} /* namespace mongo */
