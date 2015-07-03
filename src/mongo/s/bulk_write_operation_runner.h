/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "client_operation_runner.h"

namespace mongo {

class BulkWriteOperationRunner final : public ClientOperationRunner {
public:
    MONGO_DISALLOW_COPYING(BulkWriteOperationRunner);
    enum class State {
        init, running, completed, errored, finished
    };
    BulkWriteOperationRunner(network::ClientAsyncMessagePort* const connInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss);
    ~BulkWriteOperationRunner();

private:
    void processRequest() final;

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
