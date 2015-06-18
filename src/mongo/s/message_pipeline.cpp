/*
 * message_pipeline.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#include "mongo/platform/basic.h"

#include "mongo/s/message_pipeline.h"

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

void MessagePipeline::enqueueMessage(network::AsyncClientConnection* conn) {
    std::unique_lock<std::mutex> lock(_mutex);
    _newMessages.push(conn);
    lock.release();
    _notifyNewMessages.notify_one();
}

network::AsyncClientConnection* MessagePipeline::getNextMessage() {
    std::unique_lock<std::mutex> lock(_mutex);
    _notifyNewMessages.wait(lock, [this] {
        return _newMessages.size() || _terminate;
    });
    if (_terminate)
        return nullptr;
    network::AsyncClientConnection* result;
    result = _newMessages.front();
    _newMessages.pop();
    return result;
}

MessagePipeline::MessageProcessor::MessageProcessor(MessagePipeline* const owner) :
        _owner(owner) {
}

void MessagePipeline::MessageProcessor::run() {
    for (;;) {
        network::AsyncClientConnection* newMessageConn = _owner->getNextMessage();
        if (newMessageConn == nullptr)
            return;
        std::unique_ptr<OperationRunner> upRunner(new OperationRunner(newMessageConn));
        //Take a raw pointer for general use before ownership transfer
        _runner = upRunner.get();
        newMessageConn->setOpRuner(std::move(upRunner));

        _runner->run();
        //Original code releases sharded connections
        // Release connections back to pool, if any still cached
        //ShardConnection::releaseMyConnections();
    }
}

} // namespace mongo
