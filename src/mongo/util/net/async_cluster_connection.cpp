/*
 * async_client_connection.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "async_cluster_connection.h"

namespace mongo {
namespace network {

AsyncClusterConnection::AsyncClusterConnection(asio::ip::tcp::socket socket):
    AsyncMessagePort(std::move(socket)) {
}

AsyncClusterConnection::~AsyncClusterConnection() {
}

} /* namespace network */
} /* namespace mongo */
