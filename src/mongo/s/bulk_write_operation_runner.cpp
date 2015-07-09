/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "mongo/s/bulk_write_operation_runner.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/write_ops/batch_upconvert.h"


namespace mongo {
BulkWriteOperationRunner::BulkWriteOperationRunner(network::ClientAsyncMessagePort* const connInfo,
        Client* clientInfo, Message* const message, DbMessage* const dbMessage,
		NamespaceString* const nss, BatchedCommandRequest::BatchType writeType) :
		ClientOperationRunner(connInfo, clientInfo, message, dbMessage, nss),
		_writer(true, 0), _request(writeType), _writeType(writeType) {
}

BulkWriteOperationRunner::~BulkWriteOperationRunner() {

}

void BulkWriteOperationRunner::asyncStart() {


    LastError* cmdLastError = &LastError::get(_clientInfo);

    {
        // Disable the last error object for the duration of the write
        LastError::Disabled disableLastError(cmdLastError);

        // TODO: if we do namespace parsing, push this to the type
        if (!_request.parseBSON(_dbName, _cmdObjBson, &_errorMsg) ||
        		!_request.isValid(&_errorMsg)) {
            // Batch parse failure
            _response.setOk(false);
            _response.setErrCode(ErrorCodes::FailedToParse);
            _response.setErrMessage(_errorMsg);
        } else {
            _writer.write(_request, &_response);
        }

        dassert(_response.isValid(NULL));
    }
}

void BulkWriteOperationRunner::asyncProcessResults() {
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
    if (_writer.getStats().hasShardStats()) {
        ClusterLastErrorInfo::get(_clientInfo)
            .addHostOpTimes(_writer.getStats().getShardStats().getWriteOpTimes());
    }

    // TODO
    // There's a pending issue about how to report response here. If we use
    // the command infra-structure, we should reuse the 'errmsg' field. But
    // we have already filed that message inside the BatchCommandResponse.
    // return response.getOk();
    _result.appendElements(_response.toBSON());





    asyncSendResponse(); setState(State::kComplete);
}

} // namespace mongo
