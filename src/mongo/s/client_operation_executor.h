/*
 * client_operation_runner.h
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#pragma once

#include <memory>

#include "mongo/s/abstract_operation_executor.h"
#include "mongo/db/commands.h"
#include "mongo/util/factory.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

//TODO: may need to separate out client operation runner and command operation runner
//TODO: Pool these
//TODO: Align, both on the object and check the data ordering
/*
 * All operations that are in the pipeline from a client derive from this.
 * _dataNss should be set by the child, the rest of the values are initialized by COE.
 *
 */
class ClientOperationExecutor : public AbstractOperationExecutor {
public:
    MONGO_DISALLOW_COPYING(ClientOperationExecutor);
    ClientOperationExecutor(network::ClientAsyncMessagePort* const port);
    ~ClientOperationExecutor();

    void run() final;

    const MSGID& requestId() const { return _requestId; }

    Client* cc() = delete;  //cc() should never be called in a COE

protected:
    void OnErrored() { };

    static const size_t logLevelOp = 0;
    void initializeCommand();


    //Runs the command synchronously - consider for cheap commands
    void runLegacyCommand();
    void runCommand();
    void processMessage();
    void runLegacyRequest();

    //Runs the async version of the command
    virtual bool asyncAvailable() { return false; }
    virtual void asyncStart() { fassert(-100, false); }
    virtual void asyncProcessResults() { fassert(-100, false); }
    //by default expects results in _result to be sent
    void asyncSendResponse();

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

    network::ClientAsyncMessagePort* const _port;
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
    const NamespaceString _executionNss;
    NamespaceString _dataNss;
    const std::string _dbName;
    std::string _errorMsg;
    int _queryOptions{};
    int _retries = 5;
    //What version the runner cases about results for.  State shard states should ++ this
    std::atomic<size_t> _runnerEpoch{};
    Timer operationRunTimer;
    bool _usedLegacy{};
};

} /* namespace mongo */
