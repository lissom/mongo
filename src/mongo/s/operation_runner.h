/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/request.h"
#include "mongo/util/net/async_messaging_port.h"


namespace mongo {

MONGO_ALIGN_TO_CACHE class OperationRunner {
public:
    enum class State { init, running, completed, errored };
    OperationRunner(network::AsyncClientConnection* const connInfo);
    ~OperationRunner() {
        //Ensure no dangling operations
        fassert(-1, operationsActive() == false);
        port->setPersistantState(currentClient.release());
    }
    void run();
    void callback();

private:
    void processRequest();
    //Restore context information, should only need to be called when it's time to coalesce a reply probably
    void onContextStart();
    //Save the context information
    void onContextEnd();

    //All functions below this line are async, so they must be able to be ran concurrently
    void setState(State state) {
        State currentState = _state.load(std::memory_order_relaxed);
        while (currentState != State::errored)
            currentState = _state.compare_exchange_weak(currentState, state, std::memory_order_relaxed);
    }

    void setErrored() {
        _state = State::errored;
        cleanup();
    }

    /*
     * Must be able to ran multiple times
     */
    void cleanup() {

        if (!operationsActive()) {
            remove();
        }
    }

    void remove() {

    }

    //TODO: Test to see if we are waiting on return values
    bool operationsActive() { return false; }

    network::AsyncClientConnection* const port;
    Message message;
    //TODO: decompose request
    Request request;
    std::atomic<State> _state{State::init};
};

//TODO: replace with template
MONGO_ALIGN_TO_CACHE class OperationsXXX {
public:

private:
    MessagePipeline* const _owner;
    std::mutex _mutex;
    //TODO: Storing messages in the current processor is going to going to lead to skewed spread,
    //perhaps move to a fixed size randomly assigned hash queue to hold operations, we assign a global connId anyway (as of this writing)
    std::unordered_set<std::unique_ptr<OperationRunner>> _runners;
};

} // namespace mongo
