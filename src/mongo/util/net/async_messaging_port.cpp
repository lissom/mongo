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

void AsyncMessagingPort::asyncSend(Message& toSend, int responseTo) {
    //TODO: get rid of nextMessageId, it's a global atomic, crypto seq. per message thread?
    toSend.header().setId(nextMessageId());
    toSend.header().setResponseTo(responseTo);
    //TODO: Piggyback data is added here, only seems relevant to kill cursor
    toSend.isSingleData() ? asyncSendSingle(toSend) : asyncSendMulti(toSend);
}

void AsyncMessagingPort::asyncSendSingle(const Message& toSend)  {
    //_connInfo->asyncSendMessage(toSend.singleData(), toSend.size());
}

void AsyncMessagingPort::asyncSendMulti(const Message& toSend) {

}

} //namespace mongo
} //namespace network
