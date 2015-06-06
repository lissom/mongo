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
 *
 * Prefix the global counters and remove them
 * 1. ConnectionId
 * 2. MessageId
 */

class Connections;
/*
 * Functions starting with async return immediately after queueing work
 * _socket is *not* cleaned up properly for the OS, the holder does this
 *
 * Do not make virtual, counting cache lines for counters
 */
MONGO_ALIGN_TO_CACHE class ConnectionInfo {
    MONGO_DISALLOW_COPYING(ConnectionInfo);
public:
    static const auto HEADERSIZE = size_t(sizeof(MSGHEADER::Value));

    ConnectionInfo(Connections* const owner,
            asio::ip::tcp::socket socket,
            ConnectionId connectionId) :
        _owner(owner),
        _socket(std::move(socket)),
        _connectionId(std::move(connectionId)),
        _buf(HEADERSIZE)
    { }

    MsgData::View& getMsgData() {
        verify(_buf.data());
        return reinterpret_cast<MsgData::View&>(*_buf.data());
    }

    void asyncReceiveMessage();
    void asyncSendMessage();

private:
    void asyncGetHeader();
    void asyncGetMessage();
    void asyncQueueMessage();
    void asyncSocketError(std::error_code ec);
    void asyncSocketShutdownRemove();

    //A cache line is 64 bytes, or 8x8 byte numbers
    std::atomic<uint64_t> _bytesIn{};
    std::atomic<uint64_t> _bytesOut{};
    std::atomic<uint64_t> _queries{};
    std::atomic<uint64_t> _inserts{};
    std::atomic<uint64_t> _updates{};
    std::atomic<uint64_t> _deletes{};
    std::atomic<uint64_t> _commands{};
    std::atomic<uint64_t> _dummy{}; //one cache line left for counters
    //pos 8, place for one more counter
    asio::ip::tcp::socket _socket;
    ConnectionId _connectionId;
    Connections* const _owner;
    //TODO: Might have to turn this into a char*, currently trying to back Message with _freeIt = false
    std::vector _buf;
};

class ListenerPool {
    uint64_t connectionCount;
};

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
 * TODO: NUMA aware handling will be added one day, so NONE of this is static
 * All funcions starting with async are calling from async functions, should not
 * take locks if at all possible
 */
class Connections {
public:
    void newConnHandler(asio::ip::tcp::socket&& socket);

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
    ~NetworkServer() final;
    void run() final;

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
    std::vector<boost::thread> _threads;

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
