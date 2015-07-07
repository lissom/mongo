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
		asio::ip::tcp::socket socket__) :
    	AsyncMessagePort(owner, std::move(socket__)) {
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

ClientAsyncMessagePort::~ClientAsyncMessagePort() {
    mongo::setThreadName(_threadName.c_str());
    if (!serverGlobalParams.quiet) {
        log() << "end connection " << socket().remote_endpoint() << std::endl;
    }
	//The operation should be gone if we are dumping the port, otherwise it will call back into
	//a deleted object
	fassert(-1, _runner.get() == nullptr);
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
