/*
 * async_client_connection.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include <memory>

#include "mongo/util/net/async_message_port.h"
#include "mongo/client/connection_string.h"

namespace mongo {
namespace network {

class AsyncClusterConnection: public AsyncMessagePort {
public:
    AsyncClusterConnection(asio::ip::tcp::socket socket);
    virtual ~AsyncClusterConnection();

};

} /* namespace network */
} /* namespace mongo */

