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
 * ClientAsyncMessagePort(CAMP) and OperationRunner(OpRunner) are tightly bound
 * CAMP starts a receive, then passes itself to a pipeline, which generates an OperationRunner
 * CAMP should not go into State::receive with an OperationRunner in existence
 * CAMP should not be in State::send without an OperationRUnner active
 * CAMP shall delete the OperationRunner at the end of the send and retain any needed client state
 */
class BasicOperationRunner {
public:
    MONGO_DISALLOW_COPYING(BasicOperationRunner);
    enum class State {
        init, running, completed, errored, finished
    };
    BasicOperationRunner(network::ClientAsyncMessagePort* const connInfo);
    ~BasicOperationRunner();

    void run();
    void callback();
    //TODO: Test to see if we are waiting on return values
    bool operationsActive() {
        return _state != State::finished;
    }

    const MSGID& requestId() const { return _requestId; }

private:
    void processRequest() {};
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

    void logException(int logLevel, const char* const messageStart, const DBException& ex);

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

    network::ClientAsyncMessagePort* const port;
    Client* const _clientInfo;
    //TODO:rename _message
    Message _m;
    //TODO:rename _dbMessage
    DbMessage _d;
    std::atomic<State> _state { State::init };
    //Save the Id out of an abundance of caution
    const MSGID _requestId;
    const Operations _requestOp;
    const NamespaceString _nss;
};

} //namespace mongo
