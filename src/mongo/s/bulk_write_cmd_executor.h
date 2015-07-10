/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/client_operation_executor.h"
#include "mongo/s/cluster_write.h"
#include "mongo/s/write_ops/batched_command_request.h"
#include "mongo/s/write_ops/batched_command_response.h"

namespace mongo {

//Not sure I'm going to use this arch, kept here just in case so I don't have to retype it
class BulkWriteCmdExecutor : public ClientOperationExecutor {
public:
    MONGO_DISALLOW_COPYING(BulkWriteCmdExecutor);
    BulkWriteCmdExecutor(network::ClientAsyncMessagePort* const connInfo, Client* clientInfo,
            Message* const message, DbMessage* const dbMessage, NamespaceString* const nss,
			BatchedCommandRequest::BatchType writeType);

	BatchedCommandRequest::BatchType writeType() const {
		return _writeType;
	}

protected:
	void buildBatchError(ErrorCodes::Error error);

private:
    bool asyncAvailable() { return true; }
    void asyncStart() override;
    void asyncProcessResults() final override;

    /*
     * Must be able to ran multiple times
     */
    void cleanup() {
        if (!operationActive()) {
            remove();
        }
    }

    void remove() {

    }

    BatchedCommandRequest _request;
	BatchedCommandResponse _response;
	ClusterWriterStats _stats;
    BatchedCommandRequest::BatchType _writeType;
};

} // namespace mongo
