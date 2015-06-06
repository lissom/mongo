/*
 * async_messaging_port.cpp
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#include "async_messaging_port.h"

namespace mongo {
namespace network {

AsyncMessagingPort::AsyncMessagingPort(ConnectionInfo* const connInfo) : _connInfo(connInfo) {
}

void asyncSend(Message& toSend, int responseTo) {
    toSend.header().setId(nextMessageId());
    toSend.header().setResponseTo(responseTo);
    //TODO: Piggyback data is added here
    toSend.isSingleData() ? asyncSendSingle(toSend) : asyncSendMulti(toSend);
}

void asyncSendSingle(const Message& toSend)  {

}

void asyncSendMulti(const Message& toSend) {

}

} //namespace mongo
} //namespace network
