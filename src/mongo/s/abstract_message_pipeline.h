/*
 * abstract_message_pipeline.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include "../util/net/async_message_port.h"

namespace mongo {
class AbstractMessagePipeline {
public:
    MONGO_DISALLOW_COPYING(AbstractMessagePipeline);
    virtual ~AbstractMessagePipeline() {}
    virtual void enqueueMessage(network::AsyncClientConnection* conn) = 0;
    virtual network::AsyncClientConnection* getNextMessage() = 0;
};
} //namespace mongo
