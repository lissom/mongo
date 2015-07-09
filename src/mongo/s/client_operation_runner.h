/*
 * client_operation_runner.h
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/db/commands.h"
#include "mongo/s/abstract_operation_runner.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

//TODO: may need to separate out client operation runner and command operation runner
class ClientOperationRunner : public AbstractOperationRunner {
public:
    MONGO_DISALLOW_COPYING(ClientOperationRunner);
    ClientOperationRunner(network::ClientAsyncMessagePort* const connInfo, Client* clientInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);
    ~ClientOperationRunner();

    void run() final;

    const MSGID& requestId() const { return _requestId; }

protected:
    //This function exists just to make it more readable
    void intializeRun();
    void processMessage();
    void runCommand();
    // Restore context information, should only need to be called when it's time to coalesce a reply probably
    void onContextStart();
    // Save the context information
    void onContextEnd();

    // Log the exception, reply to the client and set the state to errored
    // Should only be called if normal processing is no longer need
    // (normal processing maybe be in progress)
    void logExceptionAndReply(int logLevel, const char* const messageStart, const DBException& ex);
    BSONObj buildErrReply(const DBException& ex);
    void noSuchCommand(const std::string& commandName);

    network::ClientAsyncMessagePort* const port;
    Client* const _clientInfo;
    BSONObj _cmdObjBson;
    Message _protocolMessage;
    DbMessage _dbMessage;
    QueryMessage _queryMessage;
    ServiceContext::UniqueOperationContext _operationCtx;
    Command* _command = nullptr;
    BSONObjBuilder _result;
    // Save the Id out of an abundance of caution
    const MSGID _requestId;
    const Operations _requestOp;
    // TODO: Move this out, only applies to query operations
    const NamespaceString _nss;
    const std::string _dbName;
    std::string _errorMsg;
    int options{};
    int _retries = 5;
    //What version the runner cases about results for.  State shard states should ++ this
    std::atomic<size_t> _runnerEpoch{};
};

} /* namespace mongo */
