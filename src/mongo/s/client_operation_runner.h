/*
 * client_operation_runner.h
 *
 *  Created on: Jun 29, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/abstract_operation_runner.h"
#include "mongo/util/net/client_async_message_port.h"

namespace mongo {

class ClientOperationRunner : public AbstractOperationRunner {
public:
    MONGO_DISALLOW_COPYING(ClientOperationRunner);
    ClientOperationRunner(network::ClientAsyncMessagePort* const connInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);
    ~ClientOperationRunner();

    void run() final;

    const MSGID& requestId() const { return _requestId; }

protected:
    virtual void processRequest() = 0;
    //Restore context information, should only need to be called when it's time to coalesce a reply probably
    void onContextStart();
    //Save the context information
    void onContextEnd();

    void logException(int logLevel, const char* const messageStart, const DBException& ex);

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

} /* namespace mongo */
