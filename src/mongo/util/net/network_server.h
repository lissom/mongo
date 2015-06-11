/*
 * network_server.h
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

#pragma once

#include <array>
#include <atomic>
#include <tuple>
#include <vector>
#include <unordered_map>

#include "asio.hpp"
#include "mongo/db/lasterror.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/util/net/async_messaging_port.h"
#include "mongo/s/message_pipeline.h"

namespace mongo {
namespace network {

/*
 * Per socket and per message pools are needed
 */

//If this changes the mask for allocation needs to change too
const size_t NETWORK_MIN_MESSAGE_SIZE = 1024;
const size_t NETWORK_DEFAULT_STACK_SIZE = 1024 * 1024;

/*
 * Options for a network server
 */
struct NetworkOptions {
    std::string ipList;
    int port;
    int threads{};
};

class Server {
public:
    Server() {}
    virtual ~Server() {}
    virtual void run() = 0;
};

/*
 *  Network Server using ASIO
 */
class NetworkServer : public Server {
    MONGO_DISALLOW_COPYING(NetworkServer);
public:
    NetworkServer(const NetworkOptions options, MessagePipeline* const pipeline);
    ~NetworkServer() final;
    void run() final;

    void newMessageHandler(AsyncClientConnection* message);

private:
    struct Initiator {
        Initiator(asio::io_service& service, const asio::ip::tcp::endpoint& endPoint);
        asio::ip::tcp::acceptor _acceptor;
        asio::ip::tcp::socket _socket;
    };
    //Connections can outlive the server, no point presently
    std::unique_ptr<Connections> _connections;
    asio::io_service _service;
    //Holds the end points and currently waiting socket
    std::vector<boost::thread> _threads;
    MessagePipeline* const _pipeline;
    std::vector<Initiator> _endPoints;
    //Options should be last, they are very cold
    const NetworkOptions _options;

    void serviceRun();
    void startAllWaits();
    void startWait(Initiator* const initiator);
    /*
     * New conn handler
     * Only used to hand off the data
     */
    void newConnHandler(asio::ip::tcp::socket&& socket);
};

} /* namespace network */
} /* namespace mongo */

