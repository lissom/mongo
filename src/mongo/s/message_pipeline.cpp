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
    for (;threadNum > 0; --threadNum)
        _threads.emplace_back(std::thread([this]{
            MessageProcessor processor(this);
            processor.run();
    }));
}

void MessagePipeline::enqueueMessage(network::AsyncClientConnection* conn) {
    std::unique_lock<std::mutex> lock(_mutex);
    _newMessages.push(conn);
    lock.release();
    _notifyNewMessages.notify_one();
}

network::AsyncClientConnection* MessagePipeline::getNextMessage() {
    std::unique_lock<std::mutex> lock(_mutex);
    _notifyNewMessages.wait(lock, [this]{
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
        _runner = new OperationRunner(newMessageConn);

        //TODO: Queue up the runner

        _runner->run();
        //Original code releases sharded connections
        // Release connections back to pool, if any still cached
        //ShardConnection::releaseMyConnections();
    }
}

} // namespace mongo
