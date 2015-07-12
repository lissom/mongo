/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include <map>
#include <memory>

#include "mongo/base/status.h"
#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/s/commands/command_identifiers.h"
#include "mongo/s/bulk_write_cmd_executor.h"
#include "mongo/s/catalog/catalog_cache.h"
#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/chunk_manager.h"
#include "mongo/s/client/dbclient_multi_command.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/config.h"
#include "mongo/s/dbclient_shard_resolver.h"
#include "mongo/s/grid.h"
#include "mongo/s/write_ops/batch_upconvert.h"
#include "mongo/s/write_ops/batch_write_exec.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"


/*
 * TODO: Remaining items for BulkWriteCmdExector
 * #. async DBClientShardResolver - probably going to need some sort of sequence points
 * #. async catalog
 * #. async grid
 * #. async Backend
 */

namespace mongo {

namespace {

void splitIfNeeded(const NamespaceString& nss, const TargeterStats& stats) {
    if (!Chunk::ShouldAutoSplit) {
        return;
    }

    auto status = grid.catalogCache()->getDatabase(nss.db().toString());
    if (!status.isOK()) {
        warning() << "failed to get database config for " << nss
                  << " while checking for auto-split: " << status.getStatus();
        return;
    }

    std::shared_ptr<DBConfig> config = status.getValue();

    ChunkManagerPtr chunkManager;
    ShardPtr dummyShard;
    config->getChunkManagerOrPrimary(nss.ns(), chunkManager, dummyShard);

    if (!chunkManager) {
        return;
    }

    for (std::map<BSONObj, int>::const_iterator it = stats.chunkSizeDelta.begin();
         it != stats.chunkSizeDelta.end();
         ++it) {
        ChunkPtr chunk;
        try {
            chunk = chunkManager->findIntersectingChunk(it->first);
        } catch (const AssertionException& ex) {
            warning() << "could not find chunk while checking for auto-split: " << causedBy(ex);
            return;
        }

        chunk->splitIfShould(it->second);
    }
}
} //anonymous namespace
BulkWriteCmdExecutor::BulkWriteCmdExecutor(InitFrame* const frame,
                                           BatchedCommandRequest::BatchType writeType)
    : ClientOperationExecutor(frame),
      _originalRequest(writeType),
      _writeType(writeType) {}

void BulkWriteCmdExecutor::asyncStart() {
    LastError* cmdLastError = &LastError::get(_clientInfo);
    {
        // Disable the last error object for the duration of the write
        LastError::Disabled disableLastError(cmdLastError);

        // TODO: if we do namespace parsing, push this to the type
        if (!_originalRequest.parseBSON(_dbName, _cmdObjBson, &_errorMsg) ||
            !_originalRequest.isValid(&_errorMsg)) {
            buildBatchError(ErrorCodes::FailedToParse);
            return;
        }
        write();

        dassert(_response.isValid(NULL));
    }
}

void BulkWriteCmdExecutor::write() {
    // Add _ids to insert _request if req'd
    std::unique_ptr<BatchedCommandRequest> idRequest(BatchedCommandRequest::cloneWithIds(_originalRequest));
    _request = (nullptr != idRequest.get()) ? idRequest.get() : &_originalRequest;

    _dataNss = _request->getNS();
    if (!_dataNss.isValid()) {
        toBatchError(Status(ErrorCodes::InvalidNamespace, _dataNss.ns() + " is not a valid namespace"));
        return;
    }

    if (!NamespaceString::validCollectionName(_dataNss.coll())) {
        toBatchError(
            Status(ErrorCodes::BadValue, str::stream() << "invalid collection name " << _dataNss.coll()));
        return;
    }

    if (_request->sizeWriteOps() == 0u) {
        toBatchError(Status(ErrorCodes::InvalidLength, "no write ops were included in the batch"));
        return;
    }

    if (_request->sizeWriteOps() > BatchedCommandRequest::kMaxWriteBatchSize) {
        toBatchError(Status(ErrorCodes::InvalidLength,
                            str::stream() << "exceeded maximum write batch size of "
                                          << BatchedCommandRequest::kMaxWriteBatchSize));
        return;
    }

    if (_request->isInsertIndexRequest() && !_request->isValidIndexRequest(&_errorMsg)) {
        toBatchError(Status(ErrorCodes::InvalidOptions, _errorMsg));
        return;
    }

    // Config writes and shard writes are done differently
    const std::string dbName = _dataNss.db().toString();

    if (dbName == "config" || dbName == "admin") {
        grid.catalogManager()->writeConfigServerDirect(*_request, &_response);
    } else {
        ChunkManagerTargeter targeter(_request->getTargetingNSS());
        Status targetInitStatus = targeter.init();

        if (!targetInitStatus.isOK()) {
            // Errors will be reported in _response if we are unable to target
            warning() << "could not initialize targeter for"
                      << (_request->isInsertIndexRequest() ? " index" : "")
                      << " write op in collection " << _request->getTargetingNS();
        }

        DBClientShardResolver resolver;
        DBClientMultiCommand dispatcher;
        BatchWriteExec exec(&targeter, &resolver, &dispatcher);
        exec.executeBatch(*_request, &_response);

        // TODO: Move this into a separate pipeline, swap out stats and run it there
        // TODO: Move this to run after sending the reply back
        if (Chunk::ShouldAutoSplit)
            splitIfNeeded(_dataNss, *targeter.getStats());

        _stats.setShardStats(exec.releaseStats());
    }
}

void BulkWriteCmdExecutor::asyncProcessResults() {
    LastError* cmdLastError = &LastError::get(_clientInfo);
    // Populate the lastError object based on the write response
    cmdLastError->reset();
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

    switch(writeType()) {
    case BatchedCommandRequest::BatchType_Insert :
        globalOpCounters.gotInsert(numAttempts);
        break;
    case BatchedCommandRequest::BatchType_Update :
        globalOpCounters.gotUpdate(numAttempts);
        break;
    case BatchedCommandRequest::BatchType_Delete :
        globalOpCounters.gotDelete(numAttempts);
        break;
    default :
        fassert(-39489992, false);
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

    asyncSendResponse();
    setState(State::kComplete);
}

void BulkWriteCmdExecutor::buildBatchError(ErrorCodes::Error error) {
    _response.setOk(false);
    _response.setErrCode(error);
    _response.setErrMessage(_errorMsg);
}

void BulkWriteCmdExecutor::toBatchError(const Status& status) {
    _response.clear();
    _response.setErrCode(status.code());
    _response.setErrMessage(status.reason());
    _response.setOk(false);
    dassert(_response.isValid(NULL));
}
// TODO: reinstate all this after changing BulkWriteCmdExecutor base class
/*
class BulkWriteCmdInsertExecutor final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdInsertExecutor(InitFrame* const frame)
            : BulkWriteCmdExecutor(frame, BatchedCommandRequest::BatchType::BatchType_Insert) {
    }
};

static const bool registerFactoryInsert = ClientOperationExecutorFactory::registerCreator(
            CMD_BATCH_INSERT, [] (ClientOperationExecutor::InitFrame* const frame) {
    return stdx::make_unique<ClientOperationExecutor>(frame);
});

class BulkWriteCmdUpdateExecutor final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdUpdateExecutor(InitFrame* const frame)
            : BulkWriteCmdExecutor(frame, BatchedCommandRequest::BatchType::BatchType_Update) {
    }
};

static const bool registerFactoryUpdate = ClientOperationExecutorFactory::registerCreator(
            CMD_BATCH_UPDATE, [] (ClientOperationExecutor::InitFrame* const frame) {
    return stdx::make_unique<ClientOperationExecutor>(frame);
});

class BulkWriteCmdDeleteExecutor final : public BulkWriteCmdExecutor {
public:
    BulkWriteCmdDeleteExecutor(InitFrame* const frame)
            : BulkWriteCmdExecutor(frame, BatchedCommandRequest::BatchType::BatchType_Delete) {
    }
};

static const bool registerFactoryDelete = ClientOperationExecutorFactory::registerCreator(
            CMD_BATCH_DELETE, [] (ClientOperationExecutor::InitFrame* const frame) {
    return stdx::make_unique<ClientOperationExecutor>(frame);
});
*/
}  // namespace mongo
