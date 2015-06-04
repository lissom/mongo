/*
 * network_server.h
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/platform/platform_specific.h"

#include <asio/include/asio.hpp>
#include <atomic>
#include <tuple>
#include <vector>
#include <unordered_map>


namespace mongo {
namespace network {

/*
 * Per socket and per message pools are needed
 */

/*
 * afaict asio leaves state info behind, so it has problems scaling
 * to move socket's io_servce have to get the native socket handle,
 * duplicate the native socket handle, assign the handle to a new socket and io_servicet
 * unistd.h int dup(int old fd); WSADuplicateSocket();
 *
 * jemalloc arenas
 */

struct ConnectionInfo {
    asio::ip::tcp::socket _socket;
    std::string _connectionId;
    std::vector<char> _buf;

    ConnectionInfo(asio::ip::tcp::socket socket, std::string connectionId) :
        _socket(std::move(socket)),
        _connectionId(std::move(connectionId))
    { }
};

class ListenerPool {
    uint64_t connectionCount;
};

/*
 * Using unsigned ints so they can safely rollover
 * Not used as we're not gather stats presently
 */
MONGO_ALIGN_TO_CACHE struct ConnStats {
    std::atomic<uint64_t> bytesIn{};
    std::atomic<uint64_t> bytesOut{};
    const char connId[2];
};

/*
 * Thread
 */
class ConnThread {
    ConnStats connStats;
};

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
 * TODO: NUMA aware handling
 */
class Connections {
public:
    void newConnHandler(asio::ip::tcp::socket&& socket);
    void GetHeader(ConnectionInfo* conn);
    void GetMessage(ConnectionInfo* conn);

private:
    std::unordered_map<ConnectionInfo*, std::unique_ptr<ConnectionInfo>> _conns;
    uint64_t _connectionCount{};
    std::mutex _mutex; //Protects access to _conns and _connectionCount
};

/*
 *  Network Server using ASIO
 */
class NetworkServer : public Server {
public:
    NetworkServer(const NetworkOptions options, Connections* connections);
    void run() final;
    ~NetworkServer() final;

private:
    struct Initiator {
        Initiator(asio::io_service& service, const asio::ip::tcp::endpoint& endPoint);
        asio::ip::tcp::acceptor _acceptor;
        asio::ip::tcp::socket _socket;
    };
    const NetworkOptions _options;
    Connections* _connections;
    asio::io_service _service;
    //Holds the end points and currently waiting socket
    std::vector<Initiator> _endPoints;

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

#endif /* SRC_MONGO_UTIL_NET_NETWORK_SERVER_H_ */
