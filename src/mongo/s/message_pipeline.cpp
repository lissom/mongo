/*
 * message_pipeline.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "bulk_write_cmd_executor.h"
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
        network::ClientAsyncMessagePort* clientConn =
                _owner->getNextSocketWithWaitingRequest();
        if (clientConn == nullptr)
            continue;

        Message message(clientConn->getBuffer(), false);
        DbMessage dbMessage(message);
        dbMessage.markSet();
        NamespaceString nss(dbMessage.getns());

        clientConn->restoreThreadName();
        fassert(-37, clientConn->clientInfo());
    	// TODO: turn this into a factory based on message operation
        std::unique_ptr<AbstractOperationExecutor> upRunner(
                new ClientOperationExecutor(clientConn, clientConn->clientInfo(),
                        &message, &dbMessage, &nss));

        //Take a raw pointer for general use before ownership transfer
        AbstractOperationExecutor* _runner = upRunner.get();
        clientConn->setOpRunner(std::move(upRunner));
        _runner->run();
    }
}

} // namespace mongo
