/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/basic.h"

#include "mongo/db/client_basic.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/request.h"
#include "mongo/util/net/async_messaging_port.h"


namespace mongo {

MONGO_ALIGN_TO_CACHE class OperationRunner {
public:
    OperationRunner(network::AsyncClientConnection* const connInfo);
    ~OperationRunner() {
        port.setClientInfo(std::move(_client));
    }

    void run();
    void callback();

private:
    void processRequest();
    //Restore context information, should only need to be called when it's time to coalesce a reply probably
    void onContextStart();
    //Save the context information
    void onContextEnd();

    network::AsyncMessagingPort port;
    Message message;
    //TODO: decompose request
    Request request;
    std::unique_ptr<ServiceContext::UniqueClient> _client;
};

} // namespace mongo
