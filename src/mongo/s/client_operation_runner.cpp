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
#include "client_operation_runner.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/util/log.h"
#include "mongo/util/assert_util.h"

namespace mongo {

ClientOperationRunner::ClientOperationRunner(network::ClientAsyncMessagePort* const connInfo,
                                               Message* const message,
                                               DbMessage* const dbMessage,
                                               NamespaceString* const nss)
    : port(connInfo),
      _clientInfo(&cc()),
      _m(*message),
      _d(std::move(*dbMessage)),
      txn(cc().makeOperationContext()),
      _requestId(_m.header().getId()),
      _requestOp(static_cast<Operations>(_m.operation())),
      _nss(std::move(*nss)) {
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
    _d.markSet();
    // TODO: Is it necessary to ensure that markSet is ran before initializing QueryMessage?
    q = QueryMessage(_d);
}

ClientOperationRunner::~ClientOperationRunner() {
    port->persistClientState();
}

void ClientOperationRunner::run() {
    verify(_state == State::init);
    try {
        Client::initThread("conn", port);
    } catch (std::exception& e) {
        log() << "Failed to initialize operation runner thread specific variables: " << e.what();
        return setErrored();
    } catch (...) {
        log()
            << "Failed to initialize operation runner thread specific variables: unknown exception";
        return setErrored();
    }
    // TODO: Stop setting the thread name, just set them to Pipeline_NUM?
    port->setThreadName(getThreadName());
    setState(State::running);
    LOG(3) << "BasicOperationRunner::run() start ns: " << _nss << " request id: " << _requestId
           << " op: " << _requestId << " timer: " << port->messageTimer().millis() << std::endl;
    try {
        ClusterLastErrorInfo::get(_clientInfo).newRequest();
        _cmdObjBson = q.query;
        {
            BSONElement e = _cmdObjBson.firstElement();
            if (e.type() == Object &&
                (e.fieldName()[0] == '$' ? str::equals("query", e.fieldName() + 1)
                                         : str::equals("query", e.fieldName()))) {
                // Extract the embedded query object.

                if (_cmdObjBson.hasField(Query::ReadPrefField.name())) {
                    // The command has a read preference setting. We don't want
                    // to lose this information so we copy this to a new field
                    // called $queryOptions.$readPreference
                    BSONObjBuilder final_cmdObjBsonBuilder;
                    final_cmdObjBsonBuilder.appendElements(e.embeddedObject());

                    BSONObjBuilder queryOptionsBuilder(
                        final_cmdObjBsonBuilder.subobjStart("$queryOptions"));
                    queryOptionsBuilder.append(_cmdObjBson[Query::ReadPrefField.name()]);
                    queryOptionsBuilder.done();

                    _cmdObjBson = final_cmdObjBsonBuilder.obj();
                } else {
                    _cmdObjBson = e.embeddedObject();
                }
            }
        }

        BSONElement e = _cmdObjBson.firstElement();
        std::string commandName = e.fieldName();
        _command = e.type() ? Command::findCommand(commandName) : nullptr;
        if (!_command)
            return noSuchCommand(commandName);

        //TODO: Async this: remove the loop, but keep teh assser
        for(; _retries > 0; --_retries) {
            try {
                processRequest();
                return;
            } catch (StaleConfigException& e) {
                if (_retries <= 0)
                    throw e;

                log() << "retrying command: " << q.query << std::endl;

                //TODO: Is this still necessary?
                // For legacy reasons, ns may not actually be set in the exception :-(
                std::string staleNS = e.getns();
                if (staleNS.size() == 0)
                    staleNS = q.ns;

                //TODO: Async this
                ShardConnection::checkMyConnectionVersions(staleNS);
                if (_retries < 4)
                    //TODO: Async this
                    versionManager.forceRemoteCheckShardVersionCB(staleNS);
            } catch (AssertionException& e) {
                Command::appendCommandStatus(_result, e.toStatus());
                BSONObj x = _result.done();
                replyToQuery(0, port, _m, x);
                return;
            }
        }
    } catch (const AssertionException& ex) {
        logExceptionAndReply(ex.isUserAssertion() ? 1 : 0, "Assertion failed", ex);
    } catch (const DBException& ex) {
        logExceptionAndReply(0, "Exception thrown", ex);
    }
    // TODO: handle all other exceptions.  Do we want to?
    LOG(3) << "BasicOperationRunner::run() end ns: " << _nss << " request id: " << _requestId
           << " op: " << _requestId << " timer: " << port->messageTimer().millis() << std::endl;
    onContextEnd();
}

void ClientOperationRunner::noSuchCommand(const std::string& commandName) {
    Command::appendCommandStatus(
        _result, false, str::stream() << "no such cmd: " << commandName);
    _result.append("code", ErrorCodes::CommandNotFound);
    Command::unknownCommands.increment();
    setState(State::errored);
    replyToQuery(ResultFlag_ErrSet, port, _m, _result.obj());
    return;
}

BSONObj ClientOperationRunner::buildErrReply(const DBException& ex) {
    BSONObjBuilder errB;
    errB.append("$err", ex.what());
    errB.append("code", ex.getCode());
    if (!ex._shard.empty()) {
        errB.append("shard", ex._shard);
    }
    return errB.obj();
}

void ClientOperationRunner::logExceptionAndReply(int logLevel,
                                                  const char* const messageStart,
                                                  const DBException& ex) {
    LOG(logLevel) << messageStart << " while processing op " << _requestOp << " for " << _nss
                  << causedBy(ex);
    if (doesOpGetAResponse(_requestOp)) {
        //_message is passed just to extract the response to ID, so make sure it's set
        _m.header().setId(_requestId);
        replyToQuery(ResultFlag_ErrSet, port, _m, buildErrReply(ex));
    }
    // We *always* populate the last error for now
    LastError::get(cc()).setLastError(ex.getCode(), ex.what());
    setState(State::errored);
}

void ClientOperationRunner::onContextStart() {
    port->restoreClientState();
}

void ClientOperationRunner::onContextEnd() {
    port->persistClientState();
}

} /* namespace mongo */
