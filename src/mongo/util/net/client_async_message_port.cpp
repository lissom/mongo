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

void ClientAsyncMessagePort::initialize(asio::ip::tcp::socket&& socket) {
	AsyncMessagePort::initialize(std::move(socket));
	rawInit();
}

void ClientAsyncMessagePort::rawInit() {
	try {
		Client::initThread("conn", getGlobalServiceContext(), this);
		setThreadName(getThreadName());
		if (!serverGlobalParams.quiet) {
			log() << "connection accepted from " << socket().remote_endpoint() << std::endl;
		}
		persistClientState();

	} catch (std::exception& e) {
		log() << "Failed to initialize operation runner thread specific variables: "
				<< e.what();
		//TODO: return error and shutdown port
		fassert(-82, false);
	} catch (...) {
		log() << "Failed to initialize operation runner thread specific variables: unknown"
				" exception";
		//TODO: return error and shutdown port
		fassert(-83, false);
	}
	asyncReceiveStart();
}

void ClientAsyncMessagePort::retire() {
    fassert(-89, safeToDelete());
	if (!serverGlobalParams.quiet) {
		log() << "ended connection from " << socket().remote_endpoint() << std::endl;
	}
    _persistantState.release();
    AsyncMessagePort::retire();
}

void ClientAsyncMessagePort::asyncDoneReceievedMessage() {
    /*
     * This is the last possible place we check for the operation to be ready to run
     * TODO: Perf this and move this into the queue stage if real async needs it
     */
    if (!readyToRun()) {
        //TODO: Set this to log level one after testing is over
        if (_clientWaitFails == 0)
            log() << "Long wait for client to be returned" << std::endl;
        // Post is used instead of dispatch so the loop wait yields (dispatch would spin)
        socket().get_io_service().post([this] {
            asyncDoneReceievedMessage();
        });
       return;
    }
    if (_clientWaitFails != 0)
        log() << "Client returned" << std::endl;
    _owner->handlerOperationReady(this);
}

void ClientAsyncMessagePort::asyncDoneSendMessage() {
    asyncReceiveStart();
}

void ClientAsyncMessagePort::setOpRunner(std::unique_ptr<AbstractOperationExecutor> newOpRunner) {
	fassert(-84, state() == State::kOperation);
	_runner = std::move(newOpRunner);
}

void ClientAsyncMessagePort::opRunnerComplete() {
	_runner.reset();
}


} /* namespace network */
} /* namespace mongo */
