/*
 * message_pipeline.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

#include "mongo/s/operation_runner.h"
#include "mongo/s/abstract_message_pipeline.h"

namespace mongo {

struct OpStats {
    std::atomic<uint64_t> _queries { };
    std::atomic<uint64_t> _inserts { };
    std::atomic<uint64_t> _updates { };
    std::atomic<uint64_t> _deletes { };
    std::atomic<uint64_t> _commands { };
    //6-8 are empty
};

struct CurrentOp {

};
/*
 * Splitting by # of threads so we scale linearly with it
 */
class MessagePipeline final : public AbstractMessagePipeline {
public:
    MONGO_DISALLOW_COPYING(MessagePipeline);
    MessagePipeline(size_t threadNum);
    ~MessagePipeline();

    //TODO: get all the current operations
    /*
     * Iterate over the message runners for running currentOps and then the queues for "waiting" currentOps
     */
    std::unique_ptr<CurrentOp> currentOp();
    void enqueueMessage(network::AsyncClientConnection* conn) final;
    network::AsyncClientConnection* getNextMessage();

private:
    /*
     * While MessageProcessor is private to the class there will be async call
     *
     */
    struct MessageProcessor {
    public:
        MessageProcessor(MessagePipeline* const owner);
        void run();

    private:
        OperationRunner* _runner { };
        MessagePipeline* const _owner;
    };

    void workLoop();

    OpStats opStats;
    //TODO: Get a better concurrency structure
    std::mutex _mutex;
    std::condition_variable _notifyNewMessages;
    std::queue<network::AsyncClientConnection*> _newMessages;
    std::atomic<bool> _terminate { };
    std::vector<std::thread> _threads;
};

} // namespace mongo

