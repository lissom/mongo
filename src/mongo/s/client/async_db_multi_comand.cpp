/*
 * async_db_multi_comand.cpp
 *
 *  Created on: Jul 19, 2015
 *      Author: charlie
 */

#include "async_db_multi_comand.h"

namespace mongo {

AsyncDBMultiComand::AsyncDBMultiComand() {
}

AsyncDBMultiComand::~AsyncDBMultiComand() {
}

void AsyncDBMultiComand::addCommand(const ConnectionString& endpoint,
                    StringData dbName,
                    const BSONObj& request) {

}


Status AsyncDBMultiComand::recvAny(ConnectionString* endpoint, BSONSerializable* response) {

}

} /* namespace mongo */
