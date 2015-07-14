/*
 * client_operation_runner.h
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#pragma once

#include <memory>

#include "mongo/s/commands/abstract_cmd_executor.h"
#include "mongo/s/abstract_operation_executor.h"
#include "mongo/db/commands.h"
#include "mongo/util/factory.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

//TODO: may need to separate out client operation runner and command operation runner
//TODO: Pool these
//TODO: Align, both on the object and check the data ordering
//TODO: Make these reusable
//TODO: Make a reply replyToQuery, takes a BSONObjBuilder
/*
 *  All client operations in the pipeline use this class
 */
class ClientOperationExecutor final : public AbstractOperationExecutor {
public:
    MONGO_DISALLOW_COPYING(ClientOperationExecutor);
    ClientOperationExecutor(network::ClientAsyncMessagePort* const port);
    ~ClientOperationExecutor();

    void run() override;

    const MSGID& requestId() const { return _requestId; }

    Client* cc() = delete;  //cc() should never be called in a COE

    /*
     * Calls functions wrapped in handlers for exceptions
     */
    template<typename T>
    bool safeCall(T t) {
        onContextStart();
        bool execption = true;
        try {
            t();
            execption = false;
        } catch (const AssertionException& ex) {
            logExceptionAndReply(ex.isUserAssertion() ? 1 : 0, "Assertion failed", ex);
        } catch (const DBException& ex) {
            logExceptionAndReply(0, "Exception thrown", ex);
        }
        onContextEnd();
        return execption;
    }

protected:
    // Called by async operations when they're done
    void asyncNotifyResultsReady() override;
    void OnErrored() { };

    static const size_t logLevelOp = 0;
    void initializeCommon();


    //Runs the command synchronously - consider for cheap commands
    void runLegacyCommand();
    void runCommand();
    void processMessage();
    void runLegacyRequest();
    void initializeCommand();
    void processCommand();

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
    Client* const _client;
    Message _protocolMessage;
    DbMessage _dbMessage;
    ServiceContext::UniqueOperationContext _operationCtx;
    Command* _command = nullptr;
    std::unique_ptr<AbstractCmdExecutor> _executor;
    BSONObjBuilder _result;
    // Save the Id out of an abundance of caution
    const MSGID _requestId;
    const Operations _requestOp;
    // TODO: Move this out, only applies to query operations
    NamespaceString _nss;
    const std::string _dbName;
    std::string _errorMsg;
    BSONObj _cmdObjBson;
    int _retries = 5;
    Timer operationRunTimer;
    bool _usedLegacy{};
};

} /* namespace mongo */
