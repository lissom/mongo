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

std::atomic<uint64_t> connectionCount{};

ClientAsyncMessagePort::ClientAsyncMessagePort(Connections* const owner,
		asio::ip::tcp::socket socket) :
    	AsyncMessagePort(owner, std::move(socket)), _connectionId(++connectionCount) {
    try {
        Client::initThread("conn", this);
        // TODO: Stop setting the thread name, just set them to Pipeline_NUM?
        setThreadName(getThreadName());
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
}

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
