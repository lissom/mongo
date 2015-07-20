/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "mongo/s/commands/bulk_write_cmd_executor.h"
#include "mongo/s/commands/command_identifiers.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/write_ops/batch_upconvert.h"


namespace mongo {
BulkWriteCmdExecutor::BulkWriteCmdExecutor(AbstractCmdExecutor::Settings* settings,
        BatchedCommandRequest::BatchType writeType__)
    : AbstractCmdExecutor(settings),
      _originalRequest(writeType__),
      _writeType(writeType__) {}

void BulkWriteCmdExecutor::initialize() {
    LastError* cmdLastError = &LastError::get(client());
    {
        // Disable the last error object for the duration of the write
        LastError::Disabled disableLastError(cmdLastError);

        // TODO: if we do namespace parsing, push this to the type
        if (!_request->parseBSON(_dbName, *_cmdObjBson, _errorMsg) ||
            !_request->isValid(_errorMsg)) {
            return buildBatchError(ErrorCodes::FailedToParse);
        }
        //_writer.write(_request, &_response);

    }
}

void BulkWriteCmdExecutor::processResults() {
    dassert(_response.isValid(NULL));

    LastError* cmdLastError = &LastError::get(client());
    // Populate the lastError object based on the write response
    cmdLastError->reset(); //Is this actually needed?
    batchErrorToLastError(*_request, _response, cmdLastError);

    size_t numAttempts;

    if (!_response.getOk()) {
        numAttempts = 0;
    } else if (_request->getOrdered() && _response.isErrDetailsSet()) {
        // Add one failed attempt
        numAttempts = _response.getErrDetailsAt(0)->getIndex() + 1;
    } else {
        numAttempts = _request->sizeWriteOps();
    }

    if (_writeType == BatchedCommandRequest::BatchType_Insert) {
        globalOpCounters.gotInsert(numAttempts);
    } else if (_writeType == BatchedCommandRequest::BatchType_Update) {
        globalOpCounters.gotUpdate(numAttempts);
    } else if (_writeType == BatchedCommandRequest::BatchType_Delete) {
        globalOpCounters.gotDelete(numAttempts);
    }

    // Save the last opTimes written on each shard for this client, to allow GLE to work
    if (_stats.hasShardStats()) {
        ClusterLastErrorInfo::get(client())
            .addHostOpTimes(_stats.getShardStats().getWriteOpTimes());
    }

    // TODO
    // There's a pending issue about how to report response here. If we use
    // the command infra-structure, we should reuse the 'errmsg' field. But
    // we have already filed that message inside the BatchCommandResponse.
    // return response.getOk();
    _result->appendElements(_response.toBSON());
    _owner->asyncNotifyResultsReady();
    _state.setState(AsyncState::State::kComplete);
}

void BulkWriteCmdExecutor::buildBatchError(ErrorCodes::Error error) {
    _response.setOk(false);
    _response.setErrCode(error);
    _response.setErrMessage(*_errorMsg);
    processResults();
}

class BulkWriteCmdExecutorInsert final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdExecutorInsert(AbstractCmdExecutor::Settings* settings) :
        BulkWriteCmdExecutor(settings, BatchedCommandRequest::BatchType::BatchType_Insert) {
    }
};
static const bool registerBulkWriteCmdInsert = AbstractCmdExecutorFactory::registerCreator(
        CMD_BATCH_INSERT, [] (AbstractCmdExecutor::Settings* settings) {
    return AbstractCmdExecutorPtr(new BulkWriteCmdExecutorInsert(settings));
});

class BulkWriteCmdExecutorUpdate final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdExecutorUpdate(AbstractCmdExecutor::Settings* settings) :
        BulkWriteCmdExecutor(settings, BatchedCommandRequest::BatchType::BatchType_Update) {
    }
};
static const bool registerBulkWriteCmdUpdate = AbstractCmdExecutorFactory::registerCreator(
        CMD_BATCH_UPDATE, [] (AbstractCmdExecutor::Settings* settings) {
    return AbstractCmdExecutorPtr(new BulkWriteCmdExecutorUpdate(settings));
});

class BulkWriteCmdExecutorDelete final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdExecutorDelete(AbstractCmdExecutor::Settings* settings) :
        BulkWriteCmdExecutor(settings, BatchedCommandRequest::BatchType::BatchType_Delete) {
    }
};
static const bool registerBulkWriteCmdDelete = AbstractCmdExecutorFactory::registerCreator(
        CMD_BATCH_DELETE, [] (AbstractCmdExecutor::Settings* settings) {
    return AbstractCmdExecutorPtr(new BulkWriteCmdExecutorDelete(settings));
});
}  // namespace mongo

