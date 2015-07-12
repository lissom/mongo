/*
 * abstract_operation_runner.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include <atomic>
#include <functional>

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/async_state.h"
#include "mongo/util/factory.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
class AbstractOperationExecutor;
using OpRunnerPtr = std::unique_ptr<AbstractOperationExecutor>;
//This factory will only produce client ops as the creation args are different
using OpRunnerClientCreator = std::function<OpRunnerPtr(
        network::AsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss)>;
using OpRunnerClientFactory =  RegisterFactory<OpRunnerPtr, OpRunnerClientCreator>;

/*
 * AsyncMessagePort(AMP) and OperationExecutor(OpExec) are tightly bound
 * AMP starts a receive, then passes itself to a pipeline, which generates an OperationRunner
 * AMP should not go into State::receive with an OperationRunner in existence
 * AMP should not be in State::send without an OperationRUnner active
 * AMP shall delete the OperationRunner at the end of the send and retain any needed client state
 */
class AbstractOperationExecutor {
public:
    AbstractOperationExecutor() { }
    virtual ~AbstractOperationExecutor() {
    	//Ensure no dangling operations
		fassert(-666, _state.active() == false);
    }

    virtual void run() = 0;
    /*
     * Called by owned objects to let the OpExector know results are ready, the opExec should own
     * all resources so nothing should need to be returned.
     * This is a hack b/c we don't immediately surface the commands type in the message for creating
     * a class from.
     */
    virtual void results() = 0;
    bool isComplete() { return _state == AsyncState::State::kComplete; }

protected:
    void setErrored() {
        _state.setState(AsyncState::State::kError);
    }

    AsyncState _state;
};

OpRunnerPtr createOpRunnerClient(network::AsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);

} //namespace mongo
