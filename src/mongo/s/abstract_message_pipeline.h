/*
 * abstract_message_pipeline.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/util/net/client_async_message_port.h"

namespace mongo {
class AbstractMessagePipeline {
public:
    virtual ~AbstractMessagePipeline() {}
    virtual void enqueueMessage(network::ClientAsyncMessagePort* conn) = 0;
    virtual network::ClientAsyncMessagePort* getNextSocketWithWaitingRequest() = 0;
};
} //namespace mongo
