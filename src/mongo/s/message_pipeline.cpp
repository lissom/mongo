/*
 * message_pipeline.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/s/bulk_write_operation_runner.h"
#include "mongo/s/message_pipeline.h"
#include "mongo/util/log.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

MessagePipeline::MessagePipeline(size_t threadNum) :
        _threads(threadNum) {
    for (; threadNum > 0; --threadNum)
        _threads.emplace_back(std::thread([this] {
            MessageProcessor processor(this);
            processor.run();
        }));
}

MessagePipeline::~MessagePipeline() {
    _terminate = true;
}

void MessagePipeline::enqueueMessage(network::ClientAsyncMessagePort* conn) {
    std::unique_lock<std::mutex> lock(_mutex);
    _newMessages.push(conn);
    _notifyNewMessages.notify_one();
}

network::ClientAsyncMessagePort* MessagePipeline::getNextSocketWithWaitingRequest() {
    std::unique_lock<std::mutex> lock(_mutex);
    _notifyNewMessages.wait(lock, [this] {
        return _newMessages.size() || _terminate;
    });
    if (_terminate)
        return nullptr;
    network::ClientAsyncMessagePort* result;
    result = _newMessages.front();
    _newMessages.pop();
    return result;
}

MessagePipeline::MessageProcessor::MessageProcessor(MessagePipeline* const owner) :
        _owner(owner) {
}

void MessagePipeline::MessageProcessor::run() {
    while (!inShutdown()) {
        network::ClientAsyncMessagePort* newMessageConn =
                _owner->getNextSocketWithWaitingRequest();
        if (newMessageConn == nullptr)
            continue;

    	try {
    		Client::initThread("conn", newMessageConn);
    	} catch (std::exception& e) {
    		log() << "Failed to initialize operation runner thread specific variables: "
    				<< e.what();
    		//TODO: return error and shutdown port
    	} catch (...) {
    		log() << "Failed to initialize operation runner thread specific variables: unknown"
    				" exception";
    		//TODO: return error and shutdown port
    	}
        Message message(newMessageConn->getBuffer(), false);
        DbMessage dbMessage(message);
        dbMessage.markSet();
        NamespaceString nss(dbMessage.getns());
        // TODO: Use this see if the operation is legacy and handle appropriately

    	// TODO: Stop setting the thread name, just set them to Pipeline_NUM?
    	newMessageConn->setThreadName(getThreadName());

    	// TODO: turn this into a factory based on message operation
        std::unique_ptr<AbstractOperationRunner> upRunner(
                new ClientOperationRunner(newMessageConn, &cc(), &message, &dbMessage, &nss));

        //Take a raw pointer for general use before ownership transfer
        AbstractOperationRunner* _runner = upRunner.get();
        newMessageConn->setOpRunner(std::move(upRunner));
        _runner->run();
    }
}

} // namespace mongo
