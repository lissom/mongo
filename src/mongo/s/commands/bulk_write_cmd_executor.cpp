/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/db/lasterror.h"
#include "mongo/db/stats/counters.h"
#include "mongo/s/dbclient_shard_resolver.h"
#include "mongo/s/catalog/catalog_cache.h"
#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/chunk.h"
#include "mongo/s/chunk_manager.h"
#include "mongo/s/client/dbclient_multi_command.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/commands/bulk_write_cmd_executor.h"
#include "mongo/s/commands/command_identifiers.h"
#include "mongo/s/config.h"
#include "mongo/s/grid.h"
#include "mongo/s/write_ops/batch_upconvert.h"
#include "mongo/s/write_ops/batch_write_exec.h"
#include "mongo/util/log.h"


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
        if (!_originalRequest.parseBSON(_dbName, *_cmdObjBson, _errorMsg) ||
            !_originalRequest.isValid(_errorMsg)) {
            return buildBatchError(ErrorCodes::FailedToParse);
        }

        std::unique_ptr<BatchedCommandRequest> idRequest(BatchedCommandRequest::cloneWithIds(
                _originalRequest));
        _request = (nullptr != idRequest.get()) ? idRequest.get() : &_originalRequest;

        const NamespaceString& nss = _request->getNS();
        if (!nss.isValid()) {
            toBatchError(Status(ErrorCodes::InvalidNamespace, nss.ns() + " is not a valid namespace"));
            return;
        }

        if (!NamespaceString::validCollectionName(nss.coll())) {
            toBatchError(
                Status(ErrorCodes::BadValue, str::stream() << "invalid collection name " << nss.coll()));
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

        std::string errMsg;
        if (_request->isInsertIndexRequest() && !_request->isValidIndexRequest(&errMsg)) {
            toBatchError(Status(ErrorCodes::InvalidOptions, errMsg));
            return;
        }

        // Config writes and shard writes are done differently
        const std::string dbName = nss.db().toString();

        if (dbName == "config" || dbName == "admin") {
            grid.catalogManager()->writeConfigServerDirect(*_request, &_response);
        } else {
            ChunkManagerTargeter targeter(_request->getTargetingNSS());
            Status targetInitStatus = targeter.init();

            if (!targetInitStatus.isOK()) {
                // Errors will be reported in response if we are unable to target
                warning() << "could not initialize targeter for"
                          << (_request->isInsertIndexRequest() ? " index" : "")
                          << " write op in collection " << _request->getTargetingNS();
            }

            DBClientShardResolver resolver;
            DBClientMultiCommand dispatcher;
            BatchWriteExec exec(&targeter, &resolver, &dispatcher);
            exec.executeBatch(*_request, &_response);

            //TODO: Still needed or mongoD?  Move to after results are returned, interacts with stats after this line if moved?
            if (Chunk::ShouldAutoSplit) {
                splitIfNeeded(nss, *targeter.getStats());
            }

            _stats.setShardStats(exec.releaseStats());
        }
    }
    processResults();
}

void BulkWriteCmdExecutor::finalize() {
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

void BulkWriteCmdExecutor::splitIfNeeded(const NamespaceString& nss, const TargeterStats& stats) {
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


void BulkWriteCmdExecutor::toBatchError(const Status& status) {
    _response.clear();
    _response.setErrCode(status.code());
    _response.setErrMessage(status.reason());
    _response.setOk(false);
    dassert(_response.isValid(nullptr));
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

