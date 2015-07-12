/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/commands/abstract_cmd_executor.h"
#include "mongo/s/fast_sync_container.h"
#include "mongo/s/chunk_manager_targeter.h"
#include "mongo/s/client_operation_executor.h"
#include "mongo/s/cluster_write.h"
#include "mongo/s/write_ops/batched_command_request.h"
#include "mongo/s/write_ops/batched_command_response.h"

namespace mongo {

class BulkWriteCmdExecutor : public AbstractCmdExecutor {
public:
    MONGO_DISALLOW_COPYING(BulkWriteCmdExecutor);
    BulkWriteCmdExecutor(BatchedCommandRequest::BatchType writeType);

	BatchedCommandRequest::BatchType writeType() const {
		return _writeType;
	}

	void run();

protected:
	void buildBatchError(ErrorCodes::Error error);
	void toBatchError(const Status& status);

private:

    FastSyncBSONObjPtr _results;
    BatchedCommandRequest _originalRequest;
    BatchedCommandRequest* _request{};
	BatchedCommandResponse _response;
	ClusterWriterStats _stats;
    BatchedCommandRequest::BatchType _writeType;
};

} // namespace mongo
