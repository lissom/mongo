/*
 * client_async_message_port.cpp
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#include "mongo/util/net/client_async_message_port.h"

namespace mongo {
namespace network {


ClientAsyncMessagePort::ClientAsyncMessagePort(Connections* const owner,
		asio::ip::tcp::socket socket) :
    	AsyncMessagePort(owner, std::move(socket)) { }

ClientAsyncMessagePort::~ClientAsyncMessagePort() {
}

void ClientAsyncMessagePort::setOpRunner(std::unique_ptr<AbstractOperationRunner> newOpRunner) {
	fassert(-1, state() != State::complete);
	_runner = std::move(newOpRunner);
}

} /* namespace network */
} /* namespace mongo */
