/*
 * async_client_connection.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/util/net/async_message_port.h"

namespace mongo {
namespace network {

class AsyncClientConnection: public AsyncMessagePort {
public:
    AsyncClientConnection(asio::ip::tcp::socket socket);
    virtual ~AsyncClientConnection();
};

} /* namespace network */
} /* namespace mongo */

