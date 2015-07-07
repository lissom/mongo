/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "client_operation_runner.h"

namespace mongo {

//Not sure I'm going to use this arch, kept here just in case so I don't have to retype it
class BulkWriteOperationRunner final : public ClientOperationRunner {
public:
    MONGO_DISALLOW_COPYING(BulkWriteOperationRunner);
    enum class State {
        init, running, completed, errored, finished
    };
    BulkWriteOperationRunner(network::ClientAsyncMessagePort* const connInfo, Client* clientInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);
    ~BulkWriteOperationRunner();

private:
    /*
     * Must be able to ran multiple times
     */
    void cleanup() {

        if (!operationsActive()) {
            remove();
        }
    }

    void remove() {

    }
};

} // namespace mongo
