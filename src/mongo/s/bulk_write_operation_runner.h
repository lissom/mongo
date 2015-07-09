/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/client_operation_runner.h"
#include "mongo/s/write_ops/batched_command_request.h"

namespace mongo {

//Not sure I'm going to use this arch, kept here just in case so I don't have to retype it
class BulkWriteOperationRunner final : public ClientOperationRunner {
public:
    MONGO_DISALLOW_COPYING(BulkWriteOperationRunner);
    BulkWriteOperationRunner(network::ClientAsyncMessagePort* const connInfo, Client* clientInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss,
			BatchedCommandRequest::BatchType writeType);
    ~BulkWriteOperationRunner();

private:
    void start();
    void complete();

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

    ClusterWriter _writer;
    BatchedCommandRequest _request;
	BatchedCommandResponse _response;
    BatchedCommandRequest::BatchType _writeType;
};

} // namespace mongo
