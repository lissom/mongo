/*
 * abstract_operation_runner.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"

namespace mongo {
namespace network {
    class ClientAsyncMessagePort;
}

/*
 * AsyncMessagePort(AMP) and OperationRunner(OpRunner) are tightly bound
 * AMP starts a receive, then passes itself to a pipeline, which generates an OperationRunner
 * AMP should not go into State::receive with an OperationRunner in existence
 * AMP should not be in State::send without an OperationRUnner active
 * AMP shall delete the OperationRunner at the end of the send and retain any needed client state
 */
class AbstractOperationRunner {
public:
    enum class State {
        init, running, completed, errored, finished
    };
    AbstractOperationRunner() { }
    virtual ~AbstractOperationRunner() {
    	//Ensure no dangling operations
		fassert(-1, operationsActive() == false);
    }


    virtual void run() = 0;

    //TODO: Test to see if we are waiting on return values
    bool operationsActive() {
        return _state != State::finished;
    }

protected:
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

    // Must be multithreaded safe
    virtual void OnErrored() { };

    void setErrored() {
        _state = State::errored;
        OnErrored();
    }

    std::atomic<State> _state { State::init };
};

} //namespace mongo
