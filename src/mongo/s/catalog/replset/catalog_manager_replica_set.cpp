/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/catalog/replset/catalog_manager_replica_set.h"

#include <pcrecpp.h>

#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/remote_command_targeter.h"
#include "mongo/db/commands.h"
#include "mongo/db/namespace_string.h"
#include "mongo/executor/task_executor.h"
#include "mongo/s/catalog/dist_lock_manager.h"
#include "mongo/s/catalog/type_collection.h"
#include "mongo/s/catalog/type_database.h"
#include "mongo/s/catalog/type_settings.h"
#include "mongo/s/catalog/type_shard.h"
#include "mongo/s/client/shard.h"
#include "mongo/s/write_ops/batched_command_request.h"
#include "mongo/s/write_ops/batched_command_response.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/time_support.h"

namespace mongo {

    using std::set;
    using std::string;
    using std::vector;

namespace {

    const Status notYetImplemented(ErrorCodes::InternalError, "Not yet implemented"); // todo remove
    const ReadPreferenceSetting kConfigWriteSelector(ReadPreference::PrimaryOnly, TagSet{});
    const ReadPreferenceSetting kConfigReadSelector(ReadPreference::SecondaryOnly, TagSet{});
    const Seconds kConfigCommandTimeout{30};
    const int kNotMasterNumRetries = 3;
    const Milliseconds kNotMasterRetryInterval{500};

    void _toBatchError(const Status& status, BatchedCommandResponse* response) {
        response->clear();
        response->setErrCode(status.code());
        response->setErrMessage(status.reason());
        response->setOk(false);
    }

} // namespace

    CatalogManagerReplicaSet::CatalogManagerReplicaSet() = default;

    CatalogManagerReplicaSet::~CatalogManagerReplicaSet() = default;

    Status CatalogManagerReplicaSet::init(std::unique_ptr<DistLockManager> distLockManager) {
        _distLockManager = std::move(distLockManager);
        return Status::OK();
    }

    void CatalogManagerReplicaSet::shutDown() {
        LOG(1) << "CatalogManagerReplicaSet::shutDown() called.";
        {
            boost::lock_guard<boost::mutex> lk(_mutex);
            _inShutdown = true;
        }

        invariant(_distLockManager);
        _distLockManager->shutDown();
    }

    Status CatalogManagerReplicaSet::enableSharding(const std::string& dbName) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::shardCollection(const string& ns,
                                                     const ShardKeyPattern& fieldsAndOrder,
                                                     bool unique,
                                                     vector<BSONObj>* initPoints,
                                                     set<ShardId>* initShardsIds) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::createDatabase(const std::string& dbName) {
        return notYetImplemented;
    }

    StatusWith<string> CatalogManagerReplicaSet::addShard(
            const string& name,
            const ConnectionString& shardConnectionString,
            const long long maxSize) {
        return notYetImplemented;
    }

    StatusWith<ShardDrainingStatus> CatalogManagerReplicaSet::removeShard(OperationContext* txn,
                                                                          const std::string& name) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::updateDatabase(const std::string& dbName,
                                                    const DatabaseType& db) {
        fassert(28684, db.validate());

        return notYetImplemented;
    }

    StatusWith<DatabaseType> CatalogManagerReplicaSet::getDatabase(const std::string& dbName) {
        invariant(nsIsDbOnly(dbName));
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::updateCollection(const std::string& collNs,
                                                      const CollectionType& coll) {
        fassert(28683, coll.validate());

        return notYetImplemented;
    }

    StatusWith<CollectionType> CatalogManagerReplicaSet::getCollection(const std::string& collNs) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::getCollections(const std::string* dbName,
                                                    std::vector<CollectionType>* collections) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::dropCollection(const std::string& collectionNs) {
        return notYetImplemented;
    }

    void CatalogManagerReplicaSet::logAction(const ActionLogType& actionLog) {

    }

    void CatalogManagerReplicaSet::logChange(OperationContext* opCtx,
                                             const string& what,
                                             const string& ns,
                                             const BSONObj& detail) {
    }

    StatusWith<SettingsType> CatalogManagerReplicaSet::getGlobalSettings(const string& key) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::getDatabasesForShard(const string& shardName,
                                                        vector<string>* dbs) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::getChunks(const Query& query,
                                               int nToReturn,
                                               vector<ChunkType>* chunks) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::getTagsForCollection(const std::string& collectionNs,
                                std::vector<TagsType>* tags) {
        return notYetImplemented;
    }

    StatusWith<string> CatalogManagerReplicaSet::getTagForChunk(const std::string& collectionNs,
                                                                const ChunkType& chunk) {
        return notYetImplemented;
    }

    Status CatalogManagerReplicaSet::getAllShards(vector<ShardType>* shards) {
        return notYetImplemented;
    }

    bool CatalogManagerReplicaSet::isShardHost(const ConnectionString& connectionString) {
        return false;
    }

    bool CatalogManagerReplicaSet::doShardsExist() {
        return false;
    }

    bool CatalogManagerReplicaSet::runUserManagementWriteCommand(const std::string& commandName,
                                                                 const std::string& dbname,
                                                                 const BSONObj& cmdObj,
                                                                 BSONObjBuilder* result) {
        return false;
    }

    bool CatalogManagerReplicaSet::runUserManagementReadCommand(const std::string& dbname,
                                                                const BSONObj& cmdObj,
                                                                BSONObjBuilder* result) {
        return false;
    }

    Status CatalogManagerReplicaSet::applyChunkOpsDeprecated(const BSONArray& updateOps,
                                                             const BSONArray& preCondition) {
        return notYetImplemented;
    }

    DistLockManager* CatalogManagerReplicaSet::getDistLockManager() {
        invariant(_distLockManager);
        return _distLockManager.get();
    }

    void CatalogManagerReplicaSet::writeConfigServerDirect(
            const BatchedCommandRequest& batchRequest,
            BatchedCommandResponse* batchResponse) {
    }

} // namespace mongo
