/*
 * basic_operation_runner.cpp
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/client.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/lasterror.h"
#include "mongo/s/basic_operation_runner.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

BasicOperationRunner::BasicOperationRunner(network::ClientAsyncMessagePort* const connInfo) :
        port(connInfo), _clientInfo(&cc()), _m(static_cast<void*>(connInfo->getBuffer()), false),
        _d(_m), _requestId(_m.header().getId()), _requestOp(static_cast<Operations>(_m.operation())),
        _nss(_d.messageShouldHaveNs() ? _d.getns() : "") {
    // Noting Request::process unconditionally calls getns() which calls _d.getns() for logging
    // this should always be true
    if (_d.messageShouldHaveNs()) {
        uassert(ErrorCodes::IllegalOperation,
                "can't use 'local' database through mongos",
                _nss.db() != "local");

        uassert(ErrorCodes::InvalidNamespace,
                str::stream() << "Invalid ns [" << _nss.ns() << "]",
                _nss.isValid());
    }
}

BasicOperationRunner::~BasicOperationRunner() {
    //Ensure no dangling operations
    fassert(-1, operationsActive() == false);
    port->persistClientState();
}

void BasicOperationRunner::run() {
    verify(_state == State::init);
    try {
        Client::initThread("conn", port);
    } catch (std::exception &e) {
        log() << "Failed to initialize operation runner thread specific variables: " << e.what();
        return setErrored();
    } catch (...) {
        log() << "Failed to initialize operation runner thread specific variables: unknown exception";
        return setErrored();
    }
    port->setThreadName(getThreadName());
    setState(State::running);
    LOG(3) << "BasicOperationRunner::run() start ns: " << _nss << " request id: " << _requestId
                << " op: " << _requestId << " timer: " << port->messageTimer().millis()
                << std::endl;
    try {
        ClusterLastErrorInfo::get(_clientInfo).newRequest();
        _d.markSet();
        processRequest();
    } catch (const AssertionException& ex) {
        logException(ex.isUserAssertion() ? 1 : 0, "Assertion failed", ex);
    } catch (const DBException& ex) {
        logException(0, "Exception thrown", ex);
    }
    //TODO: handle all other exceptions.  Do we want to?
    LOG(3) << "BasicOperationRunner::run() end ns: " << _nss << " request id: " << _requestId
            << " op: " << _requestId << " timer: " << port->messageTimer().millis()
            << std::endl;
    onContextEnd();
}

namespace {
BSONObj buildErrReply(const DBException& ex) {
    BSONObjBuilder errB;
    errB.append("$err", ex.what());
    errB.append("code", ex.getCode());
    if (!ex._shard.empty()) {
        errB.append("shard", ex._shard);
    }
    return errB.obj();
}
}

void BasicOperationRunner::logException(int logLevel, const char* const messageStart,
        const DBException& ex) {
    LOG(logLevel) << messageStart << " while processing op " << _requestOp
            << " for " << _nss << causedBy(ex);
    if (doesOpGetAResponse(_requestOp)) {
        //_message is passed just to extract the response to ID, so make sure it's set
        _m.header().setId(_requestId);
        replyToQuery(ResultFlag_ErrSet, port, _m, buildErrReply(ex));
    }
    // We *always* populate the last error for now
    LastError::get(cc()).setLastError(ex.getCode(), ex.what());
}

void BasicOperationRunner::onContextStart() {
    port->restoreClientState();
}

void BasicOperationRunner::onContextEnd() {
    port->persistClientState();
}

} //namespace mongo
