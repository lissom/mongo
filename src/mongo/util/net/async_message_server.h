/*
 * network_server.h
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <tuple>
#include <vector>
#include <unordered_map>

#include "mongo/util/concurrency/unbounded_container.h"
#include "mongo/db/lasterror.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/abstract_message_pipeline.h"
#include "mongo/util/net/client_async_message_port.h"
#include "mongo/util/net/message_server.h"
#include "mongo/util/net/clock.h"

namespace mongo {
namespace network {

/*
 * Per socket and per message pools are needed
 */

//If this changes the mask for allocation needs to change too
const size_t NETWORK_MIN_MESSAGE_SIZE = 1024;
const size_t NETWORK_DEFAULT_STACK_SIZE = 1024 * 1024;

/*
 *  Network MessageServer using ASIO
 */
class AsioAsyncServer: public MessageServer {
public:
    MONGO_DISALLOW_COPYING(AsioAsyncServer);
    //using Options = MessageServer::Options;
    AsioAsyncServer(const Options options, AbstractMessagePipeline* const pipeline);
    ~AsioAsyncServer() final;
    void run() final;

    /*
     * Connections call this to initiate listening on their client connection
     * and capture of a message
     */
    void handlerOperationReady(AsyncMessagePort* conn);

private:
    struct Initiator {
        Initiator(asio::io_service& service, const asio::ip::tcp::endpoint& endPoint);
        asio::ip::tcp::acceptor _acceptor;
        asio::ip::tcp::socket _socket;
    };

    void serviceRun();
    void startAllWaits();
    void startWait(Initiator* const initiator);
    void updateTime();

    //Connections can outlive the server, no point presently
    std::unique_ptr<Connections> _connections;
    asio::io_service _ioService;
    //Holds the end points and currently waiting socket
    std::vector<boost::thread> _threads;
    AbstractMessagePipeline* const _pipeline;
    std::vector<Initiator> _endPoints;
    //Options should be last, they are very cold
    const Options _options;
    boost::thread _timerThread;
};

} /* namespace network */

bool ifListenerWaitReady2();
} /* namespace mongo */

