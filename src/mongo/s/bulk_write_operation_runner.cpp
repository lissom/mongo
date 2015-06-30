/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/s/bulk_write_operation_runner.h"

namespace mongo {
BulkWriteOperationRunner::BulkWriteOperationRunner(network::ClientAsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss) :
		ClientOperationRunner(connInfo, message, dbMessage, nss) {
}

BulkWriteOperationRunner::~BulkWriteOperationRunner() {

}

void BulkWriteOperationRunner::processRequest() {

}

} // namespace mongo
