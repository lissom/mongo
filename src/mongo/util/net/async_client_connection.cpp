/*
 * async_client_connection.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "../../util/net/async_client_connection.h"

namespace mongo {
namespace network {

AsyncClientConnection::AsyncClientConnection(asio::ip::tcp::socket socket):
    AsyncMessagePort(std::move(socket)) {
}

AsyncClientConnection::~AsyncClientConnection() {
}

} /* namespace network */
} /* namespace mongo */
