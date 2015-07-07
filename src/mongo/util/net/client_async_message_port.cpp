/*
 * client_async_message_port.cpp
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/db/client.h"
#include "mongo/db/service_context.h"
#include "mongo/util/log.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {
namespace network {

ClientAsyncMessagePort::ClientAsyncMessagePort(Connections* const owner,
		asio::ip::tcp::socket socket) :
    	AsyncMessagePort(owner, std::move(socket)) {
	rawInit();
}

ClientAsyncMessagePort::~ClientAsyncMessagePort() {
	//This class should be deleted in the cache
	fassert(-1, state() == State::kWait);
}

void ClientAsyncMessagePort::initialize(asio::ip::tcp::socket&& socket) {
	AsyncMessagePort::initialize(std::move(socket));
	rawInit();
}

void ClientAsyncMessagePort::rawInit() {
	try {
		Client::initThread("conn", this);
		setThreadName(getThreadName());
		if (!serverGlobalParams.quiet) {
			log() << "connection accepted from " << socket().remote_endpoint() << std::endl;
		}
		persistClientState();

	} catch (std::exception& e) {
		log() << "Failed to initialize operation runner thread specific variables: "
				<< e.what();
		//TODO: return error and shutdown port
		fassert(-1, false);
	} catch (...) {
		log() << "Failed to initialize operation runner thread specific variables: unknown"
				" exception";
		//TODO: return error and shutdown port
		fassert(-1, false);
	}
	asyncReceiveStart();
}

void ClientAsyncMessagePort::retire() {
	restoreThreadname();
	if (!serverGlobalParams.quiet) {
		log() << "ended connection from " << socket().remote_endpoint() << std::endl;
	}
    _persistantState.release();
    AsyncMessagePort::retire();
}

void ClientAsyncMessagePort::asyncDoneReceievedMessage() {
    _owner->handlerOperationReady(this);
}

void ClientAsyncMessagePort::asyncDoneSendMessage() {
    asyncReceiveStart();
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
