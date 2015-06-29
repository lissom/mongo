/*
 * client_operation_runner.cpp
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/client.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/lasterror.h"
#include "mongo/s/client_operation_runner.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/util/log.h"
#include "mongo/util/assert_util.h"

namespace mongo {

ClientOperationRunner::ClientOperationRunner(network::ClientAsyncMessagePort* const connInfo) :
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

ClientOperationRunner::~ClientOperationRunner() {
    port->persistClientState();
}

void ClientOperationRunner::run() {
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

void ClientOperationRunner::logException(int logLevel, const char* const messageStart,
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

void ClientOperationRunner::onContextStart() {
    port->restoreClientState();
}

void ClientOperationRunner::onContextEnd() {
    port->persistClientState();
}

} /* namespace mongo */
