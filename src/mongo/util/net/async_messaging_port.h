/*
 * async_messaging_port.h
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/basic.h"

#include <algorithm>
#include <boost/thread/thread.hpp>
#include <errno.h>
#include <utility>

#include "mongo/util/assert_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"
#include "mongo/util/net/network_server.h"

namespace mongo {
namespace network {

/*
 * Piggybacking isn't supported for lazy kill cursor, not sure if that is really needed?
 * Also, if it is, it's not part of AbstractMessagingPort....
 */
class AsyncMessagingPort: public AbstractMessagingPort {
public:
    AsyncMessagingPort(ConnectionInfo* const connInfo);
    virtual ~AsyncMessagingPort() {};

    void reply(Message& received, Message& response, MSGID responseTo) final {
        asyncSend(received, responseTo);
    }
    void reply(Message& received, Message& response) final {
        asyncSend(received, response.header().getId());
    }

    /*
     * All of the below function expose implementation details and shouldn't exist
     * Consider returning std::string for error logging, etc.
     */
    //Only used for mongoD and MessagingPort... and that is pretty iffy...
    HostAndPort remote() const final { fassert(-2, false); return SockAddr(); }
    //Only used for an error string for sasl logging
    //TODO: fix sasl logging to use a string
    std::string localAddrString() const final;

private:
    ConnectionInfo* const _connInfo;

    void asyncSend(Message& toSend, int responseTo = 0);
    void asyncSendSingle(const Message& toSend);
    void asyncSendMulti(const Message& toSend);
};

} //namespace mongo
} //namespace network

