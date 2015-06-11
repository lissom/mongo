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

#include "operation_runner.h"

namespace mongo {

class MessagePipeline;
class AsyncClientConnection;

struct OpStats {
    std::atomic<uint64_t> _queries{};
    std::atomic<uint64_t> _inserts{};
    std::atomic<uint64_t> _updates{};
    std::atomic<uint64_t> _deletes{};
    std::atomic<uint64_t> _commands{};
    //6-8 are empty
};

/*
 * Splitting by # of threads so we scale linearly with it
 */
class MessagePipeline {
public:
    MessagePipeline(size_t threadNum);
    ~MessagePipeline() {
        _terminate = true;
    };

    void enqueueMessage(AsyncClientConnection* conn);
    AsyncClientConnection* getNextMessage();

private:
    /*
     * While MessageProcessor is private to the class there will be async call
     *
     */
    class MessageProcessor {
    public:
        MessageProcessor();
        void run();

    private:
        MessagePipeline* const _owner;
        std::mutex _mutex;
        //TODO: Storing messages in the current processor is going to going to lead to skewed spread,
        //perhaps move to a fixed size randomly assigned hash queue to hold operations, we assign a global connId anyway (as of this writing)
        std::unordered_set<std::unique_ptr<OperationRunner>> _runners;
    };

    void workLoop();

    OpStats opStats;
    //TODO: Get a better concurrency structure
    std::mutex _mutex;
    std::condition_variable _notifyNewMessages;
    std::queue<AsyncClientConnection*> _newMessages;
    std::atomic<bool> _terminate{};
    std::vector<std::thread> _threads;
};

} // namespace mongo

