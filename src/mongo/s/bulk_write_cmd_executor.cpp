/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "bulk_write_cmd_executor.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/write_ops/batch_upconvert.h"


namespace mongo {
BulkWriteCmdExecutor::BulkWriteCmdExecutor(network::ClientAsyncMessagePort* const connInfo,
                                                   Client* clientInfo,
                                                   Message* const message,
                                                   DbMessage* const dbMessage,
                                                   NamespaceString* const nss,
                                                   BatchedCommandRequest::BatchType writeType)
    : ClientOperationExecutor(connInfo, clientInfo, message, dbMessage, nss),
      _request(writeType),
      _writeType(writeType) {}

void BulkWriteCmdExecutor::asyncStart() {
    LastError* cmdLastError = &LastError::get(_clientInfo);
    {
        // Disable the last error object for the duration of the write
        LastError::Disabled disableLastError(cmdLastError);

        // TODO: if we do namespace parsing, push this to the type
        if (!_request.parseBSON(_dbName, _cmdObjBson, &_errorMsg) ||
            !_request.isValid(&_errorMsg)) {
            buildBatchError(ErrorCodes::FailedToParse);
            return;
        }
        //_writer.write(_request, &_response);

        dassert(_response.isValid(NULL));
    }
}

void BulkWriteCmdExecutor::asyncProcessResults() {
    LastError* cmdLastError = &LastError::get(_clientInfo);
    // Populate the lastError object based on the write response
    cmdLastError->reset();
    batchErrorToLastError(_request, _response, cmdLastError);

    size_t numAttempts;

    if (!_response.getOk()) {
        numAttempts = 0;
    } else if (_request.getOrdered() && _response.isErrDetailsSet()) {
        // Add one failed attempt
        numAttempts = _response.getErrDetailsAt(0)->getIndex() + 1;
    } else {
        numAttempts = _request.sizeWriteOps();
    }

    // TODO: increase opcounters by more than one
    if (_writeType == BatchedCommandRequest::BatchType_Insert) {
        for (size_t i = 0; i < numAttempts; ++i) {
            globalOpCounters.gotInsert();
        }
    } else if (_writeType == BatchedCommandRequest::BatchType_Update) {
        for (size_t i = 0; i < numAttempts; ++i) {
            globalOpCounters.gotUpdate();
        }
    } else if (_writeType == BatchedCommandRequest::BatchType_Delete) {
        for (size_t i = 0; i < numAttempts; ++i) {
            globalOpCounters.gotDelete();
        }
    }

    // Save the last opTimes written on each shard for this client, to allow GLE to work
    if (_stats.hasShardStats()) {
        ClusterLastErrorInfo::get(_clientInfo)
            .addHostOpTimes(_stats.getShardStats().getWriteOpTimes());
    }

    // TODO
    // There's a pending issue about how to report response here. If we use
    // the command infra-structure, we should reuse the 'errmsg' field. But
    // we have already filed that message inside the BatchCommandResponse.
    // return response.getOk();
    _result.appendElements(_response.toBSON());


    setState(State::kComplete);
    asyncSendResponse();
}

void BulkWriteCmdExecutor::buildBatchError(ErrorCodes::Error error) {
    _response.setOk(false);
    _response.setErrCode(error);
    _response.setErrMessage(_errorMsg);
}
}  // namespace mongo
