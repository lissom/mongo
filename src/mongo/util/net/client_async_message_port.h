/*
 * client_async_message_port.h
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/abstract_operation_runner.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
namespace network {

//TODO: Release the _runner after send
class ClientAsyncMessagePort : public AsyncMessagePort {
public:
    MONGO_DISALLOW_COPYING(ClientAsyncMessagePort);
    ClientAsyncMessagePort(Connections* const owner, asio::ip::tcp::socket socket);
    ~ClientAsyncMessagePort();

    void setOpRunner(std::unique_ptr<AbstractOperationRunner> newOpRunner);

private:
    std::unique_ptr<AbstractOperationRunner> _runner;

};

} /* namespace network */
} /* namespace mongo */

