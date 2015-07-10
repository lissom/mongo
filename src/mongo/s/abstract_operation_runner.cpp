/*
 * basic_operation_runner.cpp
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "abstract_operation_executor.h"

namespace mongo {
OpRunnerPtr createOpRunnerClient(network::ClientAsyncMessagePort* const connInfo,
        Message* const message, DbMessage* const dbMessage, NamespaceString* const nss) {
    OpRunnerPtr ret;
    // TODO: Move generating the right OpRunner into commands, here now to minimize files touched


    return ret;
}

} //namespace mongo
