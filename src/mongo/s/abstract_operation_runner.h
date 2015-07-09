/*
 * abstract_operation_runner.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include <functional>

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/util/factory.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
class AbstractOperationRunner;
using OpRunnerPtr = std::unique_ptr<AbstractOperationRunner>;
//This factory will only produce client ops as the creation args are different
using OpRunnerClientCreator = std::function<OpRunnerPtr(
        network::AsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss)>;
using OpRunnerClientFactory =  RegisterFactory<OpRunnerPtr, OpRunnerClientCreator>;

/*
 * AsyncMessagePort(AMP) and OperationRunner(OpRunner) are tightly bound
 * AMP starts a receive, then passes itself to a pipeline, which generates an OperationRunner
 * AMP should not go into State::receive with an OperationRunner in existence
 * AMP should not be in State::send without an OperationRUnner active
 * AMP shall delete the OperationRunner at the end of the send and retain any needed client state
 */
class AbstractOperationRunner {
public:
	/*
	 * kInit - starting
	 * kRunning - processing outgoing requests
	 * kWait - waiting for io requests to complete
	 * kError - A fatal event took place, the operation should end as soon as possible
	 * kComplete - all operations are complete
	 *
	 * kError should be treated as a modified kWait where operations can be canceled and results
	 * can be thrown away.
	 */
    enum class State {
        kInit, kRunning, kWait, kError, kComplete
    };

    AbstractOperationRunner() { }
    virtual ~AbstractOperationRunner() {
    	//Ensure no dangling operations
		fassert(-666, operationActive() == false);
    }

    virtual void run() = 0;

    State state() const { return _state; }
    //TODO: Test to see if we are waiting on return values
    bool operationActive() {
        return _state != State::kComplete;
    }

protected:
    //All functions below this line are async, so they must be able to be ran concurrently
    void setState(State state) {
        State currentState = _state.load(std::memory_order_consume);
        while ( (currentState == State::kError && state != State::kComplete )
        		|| !_state.compare_exchange_weak(currentState, state, std::memory_order_acquire)) { };
    }

    // Must be multithreaded safe
    virtual void OnErrored() { };

    void setErrored() {
        _state = State::kError;
        OnErrored();
    }

private:
    std::atomic<State> _state { State::kInit };
};

OpRunnerPtr createOpRunnerClient(network::AsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);

} //namespace mongo
