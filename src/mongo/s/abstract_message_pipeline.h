/*
 * abstract_message_pipeline.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/util/net/async_messaging_port.h"

namespace mongo {
class AbstractMessagePipeline {
public:
    virtual ~AbstractMessagePipeline() {}
    virtual void enqueueMessage(network::AsyncClientConnection* conn) = 0;
    virtual network::AsyncClientConnection* getNextMessage() = 0;
};
} //namespace mongo
