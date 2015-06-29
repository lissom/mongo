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
	delete _runner;
}

void ClientAsyncMessagePort::setOpRunner(std::unique_ptr<BasicOperationRunner> newOpRunner) {
	fassert(-1, state() != State::complete);
	_runner = newOpRunner.release();
}

} /* namespace network */
} /* namespace mongo */
