/*
 * abstract_message_pipeline.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include "../util/net/async_client_message_port.h"

namespace mongo {
class AbstractMessagePipeline {
public:
    virtual ~AbstractMessagePipeline() {}
    virtual void enqueueMessage(network::AsyncClientMessagePort* conn) = 0;
    virtual network::AsyncClientMessagePort* getNextSocketWithWaitingRequest() = 0;
};
} //namespace mongo
