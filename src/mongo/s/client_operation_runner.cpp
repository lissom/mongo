/*
 * client_operation_runner.cpp
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include <thread>

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/lasterror.h"
#include "mongo/s/client_operation_runner.h"
#include "mongo/s/client/shard_connection.h" //remove
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/stale_exception.h"
#include "mongo/s/version_manager.h" //remove
#include "mongo/util/log.h"
#include "mongo/util/assert_util.h"

namespace mongo {

ClientOperationRunner::ClientOperationRunner(network::ClientAsyncMessagePort* const connInfo,
                                               Client* clientInfo,
                                               Message* const message,
                                               DbMessage* const dbMessage,
                                               NamespaceString* const nss)
    : port(connInfo),
      _clientInfo(clientInfo),
      _m(*message),
	  //dbmessage references message, so it has to be constructed here
      _d(_m),
      q(_d),
      txn(_clientInfo->makeOperationContext()),
      _result(32738),
      _requestId(_m.header().getId()),
      _requestOp(static_cast<Operations>(_m.operation())),
      _nss(std::move(*nss)) {
}

ClientOperationRunner::~ClientOperationRunner() {
    port->persistClientState();
}

void ClientOperationRunner::run() {
	port->persistClientState();
    std::thread processRequest([this]{
    		port->restoreClientState();
    		processMessage();
            Command::execCommandClientBasic(txn.get(), _command, *txn->getClient(), 0,
                    _nss.ns().c_str(), _cmdObjBson, _result);
            port->persistClientState();
            port->opRunnerComplete();
        });
    processRequest.detach();
}

void ClientOperationRunner::processMessage() {
	try {
		verify(_state == State::init);

		LOG(3) << "BasicOperationRunner::run() start ns: " << _nss << " request id: " << _requestId
			   << " op: " << _requestId << " timer: " << port->messageTimer().millis() << std::endl;

		LastError::get(_clientInfo).startRequest();
		ClusterLastErrorInfo::get(_clientInfo).newRequest();

		if (_d.messageShouldHaveNs()) {
			uassert(ErrorCodes::IllegalOperation,
					"can't use 'local' database through mongos",
					_nss.db() != "local");

			uassert(ErrorCodes::InvalidNamespace,
					str::stream() << "Invalid ns [" << _nss.ns() << "]",
					_nss.isValid());
		}

		AuthorizationSession::get(_clientInfo)->startRequest(NULL);

		_cmdObjBson = q.query;
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
			e = _cmdObjBson.firstElement();
		}

		std::string commandName = e.fieldName();
		_command = e.type() ? Command::findCommand(commandName) : nullptr;
		if (!_command)
			return noSuchCommand(commandName);

		setState(State::running);

		//TODO: Async this: remove the loop, but keep teh assser
		for(; _retries > 0; --_retries) {
			try {
				processMessage();
				return;
			//TODO: remove this, these should be handled by the completion handlers
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

	/*
	 * RESTORE AFTER SINGLE THREADED TEST IS COMPLETE
	 */
	//onContextEnd();
	setState(State::completed);
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
