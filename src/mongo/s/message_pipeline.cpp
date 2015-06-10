/*
 * message_pipeline.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#include "mongo/platform/basic.h"

#include "mongo/s/message_pipeline.h"
#include "mongo/util/net/async_messaging_port.h"
#include "mongo/util/net/network_server.h"

namespace mongo {

MessagePipeline::MessagePipeline(size_t threadNum) :
    _threads(threadNum) {
    for (;threadNum > 0; --threadNum)
        _threads.emplace_back(std::thread([this]{ workLoop(); }));
}

void MessagePipeline::enqueueMessage(AsyncClientConnection* conn) {
    std::unique_lock<std::mutex> lock(_mutex);
    _newMessages.push(conn);
    lock.release();
}

AsyncClientConnection* MessagePipeline::getNextMessage() {
    std::unique_lock<std::mutex> lock(_mutex);
    _wait.wait(lock, [this]{
        return _newMessages.size() || _terminate;
    });
    if (_terminate)
        return nullptr;
    AsyncClientConnection result;
    result = _newMessages.front();
    _newMessages.pop();
    return result;
}

void MessagePipeline::workLoop() {
    MessageProcessor processor(this);
    processor.run();
}

void MessagePipeline::MessageProcessor::run() {
    for (;;) {
        AsyncClientConnection* newMessageConn = _owner->getNextMessage();
        if (newMessageConn == nullptr)
            return;
        std::unique_ptr<OperationRunner> runner(new OperationRunner(newMessageConn));
        {
            std::unique_lock lock(_mutex);
            _runners.emplace(std::move(runner));
        }
        runner->run();

        //Original code releases sharded connections
        // Release connections back to pool, if any still cached
        //ShardConnection::releaseMyConnections();
    }
}

} // namespace mongo
