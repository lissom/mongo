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
	//The operation should be gone if we are dumping the port, otherwise it will call back into
	//a deleted object
	fassert(-1, _runner.get() == nullptr);
}

void ClientAsyncMessagePort::setOpRunner(std::unique_ptr<AbstractOperationRunner> newOpRunner) {
	fassert(-1, state() == State::kOperation);
	_runner = std::move(newOpRunner);
}

void ClientAsyncMessagePort::opRunnerComplete() {
	_runner.reset();
}


} /* namespace network */
} /* namespace mongo */
