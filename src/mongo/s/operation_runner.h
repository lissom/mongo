/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/basic.h"

#include "../util/net/async_message_port.h"
#include "mongo/db/client.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/abstract_operation_runner.h"
#include "mongo/s/request.h"

namespace mongo {

//TODO: Add owner and have the runner pop itself on finish
//TODO: MONGO_ALIGN_TO_CACHE
class OperationRunner: public AbstractOperationRunner {
public:
    enum class State {
        init, running, completed, errored, finished
    };
    OperationRunner(network::AsyncClientConnection* const connInfo);
    ~OperationRunner();

    void run();
    void callback();
    //TODO: Test to see if we are waiting on return values
    bool operationsActive() {
        return _state != State::finished;
    }

private:
    void processRequest();
    //Restore context information, should only need to be called when it's time to coalesce a reply probably
    void onContextStart();
    //Save the context information
    void onContextEnd();

    //All functions below this line are async, so they must be able to be ran concurrently
    void setState(State state) {
        State currentState = _state.load(std::memory_order_consume);
        verify(currentState != State::finished);
        while (!_state.compare_exchange_weak(currentState, state, std::memory_order_acquire)) {
            verify(currentState != State::finished);
            if (currentState == State::errored)
                break;
        }
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

    network::AsyncClientConnection* const port;
    Message message;
    //TODO: decompose request
    Request request;
    std::atomic<State> _state { State::init };
};

//TODO: replace with template
} // namespace mongo
