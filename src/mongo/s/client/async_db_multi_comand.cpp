/*
 * async_db_multi_comand.cpp
 *
 *  Created on: Jul 19, 2015
 *      Author: charlie
 */

#include "mongo/s/client/async_db_multi_comand.h"
#include "mongo/util/net/async_cluster_end_point_pool.h"

namespace mongo {

AsyncDBMultiComand::AsyncDBMultiComand() {
}

AsyncDBMultiComand::~AsyncDBMultiComand() {
}

void AsyncDBMultiComand::addCommand(const ConnectionString& endpoint,
                    StringData dbName,
                    const BSONObj& request) {
    auto outPool = network::clusterEndPointPool;

    AsyncData asyncData(endPoint);
    outPool->asyncSendData();
}


Status AsyncDBMultiComand::recvAny(ConnectionString* endpoint, BSONSerializable* response) {

}

} /* namespace mongo */
