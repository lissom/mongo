/*
 * client_operation_runner.cpp
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include <iosfwd>
#include <thread>

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "client_operation_executor.h"
#include "mongo/s/client/shard_connection.h" //remove
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/request.h" //remove when legacy operations are no longer needed
#include "mongo/s/stale_exception.h" //remove, should be handled by the callback machinery, maybe versionedClientRequest?
#include "mongo/s/version_manager.h" //remove
#include "mongo/util/log.h"
#include "mongo/util/assert_util.h"

namespace mongo {

ClientOperationExecutor::ClientOperationExecutor(network::ClientAsyncMessagePort* const connInfo,
                                               Client* clientInfo,
                                               Message* const message,
                                               DbMessage* const dbMessage,
                                               NamespaceString* const nss)
    : _port(connInfo),
      _clientInfo(clientInfo),
      _protocolMessage(*message),
	  //dbmessage references message, so it has to be constructed here
      _dbMessage(_protocolMessage),
      _queryMessage(_dbMessage),
      _operationCtx(_clientInfo->makeOperationContext()),
      _result(32768),
      _requestId(_protocolMessage.header().getId()),
      _requestOp(static_cast<Operations>(_protocolMessage.operation())),
      _nss(std::move(*nss)),
	  _dbName(_nss.ns()) {
	// TODO: b.skip(sizeof(QueryResult::Value)); on the full async skip this
}

ClientOperationExecutor::~ClientOperationExecutor() {
    // The client should always be pushed back to the socket on close
    fassert(-667, !haveClient());
}

void ClientOperationExecutor::run() {
	fassert(-20, _port->state() == network::AsyncMessagePort::State::kOperation);
	fassert(-21, state() == State::kInit);
	setState(State::kRunning);
	//TODO: Derive AsyncClientOperationRunner
	asyncAvailable() ? asyncStart() : runLegacyCommand();
}

void ClientOperationExecutor::initializeCommand() {
	LOG(logLevelOp) << "ClientOperationRunner begin ns: " << _nss << " request id: " << _requestId
		   << " op: " << opToString(_requestOp) << " timer: " << _port->messageTimer().millis() << std::endl;

	LastError::get(_clientInfo).startRequest();
	ClusterLastErrorInfo::get(_clientInfo).newRequest();

	if (_dbMessage.messageShouldHaveNs()) {
		uassert(ErrorCodes::IllegalOperation,
				"can't use 'local' database through mongos",
				_nss.db() != "local");

		uassert(ErrorCodes::InvalidNamespace,
				str::stream() << "Invalid ns [" << _nss.ns() << "]",
				_nss.isValid());
	}

	_cmdObjBson = _queryMessage.query;
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

	int n = _dbMessage.getQueryNToReturn();
	uassert(-16978,
	str::stream() << "bad numberToReturn (" << n
				  << ") for $cmd type ns - can only be 1 or -1",
	n == 1 || n == -1);

	std::string commandName = e.fieldName();
	_command = e.type() ? Command::findCommand(commandName) : nullptr;
	if (!_command)
		return noSuchCommand(commandName);

	AuthorizationSession::get(_clientInfo)->startRequest(NULL);
}

void ClientOperationExecutor::runLegacyCommand() {
    std::thread processRequest([this] {
        onContextStart();
        processMessage();
        if (!_usedLegacy) {
			LOG(logLevelOp) << "ClientOperationRunner end ns: " << _nss << " request id: " << _requestId
					<< " op: " << opToString(_requestOp) << " timer: " << _port->messageTimer().millis() << std::endl;
        }
        setState(State::kComplete);
		ShardConnection::releaseMyConnections();
        onContextEnd();
        // Deletes the operation runner
        _port->opRunnerComplete();
    });
    processRequest.detach();
}

void ClientOperationExecutor::processMessage() {
	try {
		// Funnel non-commands and special commands to the legacy runner
		if (!_nss.isCommand()) {
			_usedLegacy = true;
			return runLegacyRequest();
		}

		initializeCommand();
		if (!_command)
			return;

		//TODO: Async this: remove the loop
		//This loop only retries on StateConfigException
		for(;; --_retries) {
			try {
			    log() << "_cmdObjBson: " << _cmdObjBson << std::endl;
			    /*Command::execCommandClientBasic(_operationCtx.get(), _command, *_clientInfo,
			            _queryOptions, _queryMessage.ns, _cmdObjBson, _result);*/
	            runCommand();
	            asyncSendResponse();
				break;
			//TODO: remove this, these should be handled by the completion handlers
			} catch (StaleConfigException& e) {
				if (_retries <= 0)
					throw e;

				log() << "retrying command: " << _queryMessage.query << std::endl;

				//TODO: Is this still necessary?
				// For legacy reasons, ns may not actually be set in the exception :-(
				std::string staleNS = e.getns();
				if (staleNS.size() == 0)
					staleNS = _queryMessage.ns;

				//TODO: Async this
				ShardConnection::checkMyConnectionVersions(staleNS);
				if (_retries < 4)
					//TODO: Async this
					versionManager.forceRemoteCheckShardVersionCB(staleNS);
			} catch (AssertionException& e) {
				Command::appendCommandStatus(_result, e.toStatus());
				BSONObj x = _result.done();
				replyToQuery(0, _port, _protocolMessage, x);
				break;
			}
		}
	} catch (const AssertionException& ex) {
		logExceptionAndReply(ex.isUserAssertion() ? 1 : 0, "Assertion failed", ex);
	} catch (const DBException& ex) {
		logExceptionAndReply(0, "Exception thrown", ex);
	}
	// TODO: handle all other exceptions.  Do we want to?
}

void ClientOperationExecutor::runCommand() {
    std::string _dbname = nsToDatabase(_nss.ns());

	if (_cmdObjBson.getBoolField("help")) {
		std::stringstream help;
		help << "help for: " << _command->name << " ";
		_command->help(help);
		_result.append("help", help.str());
		_result.append("lockType", _command->isWriteCommandForConfigServer() ? 1 : 0);
		Command::appendCommandStatus(_result, true, "");
		return;
	}

	Status status = Command::_checkAuthorization(_command, _clientInfo, _dbname, _cmdObjBson);
	if (!status.isOK()) {
		Command::appendCommandStatus(_result, status);
		return;
	}

	_command->_commandsExecuted.increment();

	if (_command->shouldAffectCommandCounter()) {
		globalOpCounters.gotCommand();
	}

	bool ok;
	try {
		ok = _command->run(_operationCtx.get(), _dbname, _cmdObjBson, 0, _errorMsg, _result);
	} catch (const DBException& e) {
		ok = false;
		int code = e.getCode();
		if (code == RecvStaleConfigCode) {  // code for StaleConfigException
			throw;
		}

		_errorMsg = e.what();
		_result.append("code", code);
	}

	if (!ok) {
		_command->_commandsFailed.increment();
	}

	Command::appendCommandStatus(_result, ok, _errorMsg);
}

void ClientOperationExecutor::runLegacyRequest() {
	Request request(_protocolMessage, _port);
	request.init();
	request.process();
}

void ClientOperationExecutor::noSuchCommand(const std::string& commandName) {
    Command::appendCommandStatus(
        _result, false, str::stream() << "no such cmd: " << commandName);
    _result.append("code", ErrorCodes::CommandNotFound);
    Command::unknownCommands.increment();
    replyToQuery(ResultFlag_ErrSet, _port, _protocolMessage, _result.obj());
    setState(State::kComplete);
    return;
}

BSONObj ClientOperationExecutor::buildErrReply(const DBException& ex) {
    BSONObjBuilder errB;
    errB.append("$err", ex.what());
    errB.append("code", ex.getCode());
    if (!ex._shard.empty()) {
        errB.append("shard", ex._shard);
    }
    return errB.obj();
}

void ClientOperationExecutor::logExceptionAndReply(int logLevel,
                                                  const char* const messageStart,
                                                  const DBException& ex) {
    LOG(logLevel) << messageStart << " while processing op " << _requestOp << " for " << _nss
                  << causedBy(ex);
    if (doesOpGetAResponse(_requestOp)) {
        //_message is passed just to extract the response to ID, so make sure it's set
        _protocolMessage.header().setId(_requestId);
        replyToQuery(ResultFlag_ErrSet, _port, _protocolMessage, buildErrReply(ex));
    }
    // We *always* populate the last error for now
    LastError::get(_port->clientInfo()).setLastError(ex.getCode(), ex.what());
    setState(State::kError);
}

void ClientOperationExecutor::asyncSendResponse() {
	BSONObj reply = _result.done();
	replyToQuery(0, _port, _protocolMessage, reply);
}

void ClientOperationExecutor::onContextStart() {
    _port->restoreClientState();
}

void ClientOperationExecutor::onContextEnd() {
    _port->persistClientState();
}

} /* namespace mongo */
