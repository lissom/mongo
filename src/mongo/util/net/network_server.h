/*
 * network_server.h
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

#pragma once

#include <array>
#include <asio/include/asio.hpp>
#include <atomic>
#include <tuple>
#include <vector>
#include <unordered_map>

#include "mongo/db/lasterror.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/message_pipeline.h"

namespace mongo {
namespace network {

/*
 * Per socket and per message pools are needed
 */

/*
 * We really need a _simple_ buffer type (that other buffers can then derive from)
 *
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
using BufferSet = std::vector< std::pair<char*, int>>;
const auto HEADERSIZE = size_t(sizeof(MSGHEADER::Value));
class Connections;
class NetworkServer;
class AsyncClientConnection;
using NewMessageEventData = AsyncClientConnection*;

struct ConnStats {
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
}
/*
 * Functions starting with async return immediately after queueing work
 * _socket is *not* cleaned up properly for the OS, the holder does this
 *
 * Do not make virtual, counting cache lines for counters
 */
//TODO: Merge with AsyncMessagingPort, move ConnStats and Connections over too
MONGO_ALIGN_TO_CACHE class AsyncClientConnection {
    MONGO_DISALLOW_COPYING(AsyncClientConnection);
public:
    AsyncClientConnection(Connections* const owner,
            asio::ip::tcp::socket socket,
            ConnectionId connectionId) :
        _owner(owner),
        _socket(std::move(socket)),
        _connectionId(std::move(connectionId)),
        _buf(0)
    { }

    MsgData::View& getMsgData() {
        verify(_buf.data());
        return reinterpret_cast<MsgData::View&>(*_buf.data());
    }

    void closeOnComplete() { _closeOnComplete = true; }
    void asyncReceiveMessage();
    LastError& getLastError() { return _clientInfo; }
    void setClientInfo(std::unique_ptr<ServiceContext::UniqueClient> clientInfo) { 
        _clientInfo = std::move(clientInfo);
    }
    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
    std::unique_ptr<ServiceContext::UniqueClient> releaseClientInfo() { 
        return _clientInfo.release();
    }
    char* getBuffer() { return _buf.data(); }
    const ConnStats& getStats() const { return _stats; }
    ConnectionId getConnectionId() { return _connectionId; }
    //TODO: type checking of handler function
    //Data should be small, i.e. a pointer or number
    template <typename Buffer, typename Callback, typename Data>
    void asyncSendMessage(Buffer&& buffer,
            Data data = nullptr,
            Callback callback = [] (AsyncClientConnection*, size_t, std::error_code, Data) {}) {
        //We have no guarantee the sender persists
        //TODO: revisit this and see what assumptions are possible about existence
        _socket.async_send(std::forward<Buffer>(buffer),
                [this, data, callback](std::error_code ec, size_t len) {
            _stats._bytesOut += len;
            callback(this, len, ec, data);
            if (ec) {
                asyncSocketError(ec);
                return;
            }
            if (len != HEADERSIZE) {
                log() << "Error, invalid header size received: " << len << std::endl;
                asyncSocketShutdownRemove(conn);
                return;
            }
            doClose() ? asyncSocketShutdownRemove() : asyncReceiveMessage();
        });
    }

    //TODO: Just have static calls backs that record errors, at this point a reply is sent and it's over
    //For single data Message
    template <typename Callback, typename Data>
    void asyncSendMessage(const char* const buf, const size_t size,
            Data data = nullptr,
            Callback callback = [] (AsyncClientConnection*, size_t, std::error_code, Data) {}) {
        //We have no guarantee the sender persists
        //TODO: revisit this and see what assumptions are possible about existence, need to copy header regardless
        _buf.clear();
        _buf.resize(size);
        memcpy(_buf.data(), buf, size);
        asyncSendMessage(asio::const_buffer(_buf.data(), size), data, callback);
    }

    //For multi data Message
    template <typename Callback, typename Data>
    void asyncSendMessage(const char* const buf, const size_t size,
            BufferSet* buffers,
            Data data = nullptr,
            Callback callback = [] (AsyncClientConnection*, size_t, std::error_code, Data) {}) {
        //We have no guarantee the sender persists
        //TODO: revisit this and see what assumptions are possible about existence
        //Copy the header into _buf, need to do this regardless so we have the info
        _buf.clear();
        _buf.resize(HEADERSIZE);
        memcpy(_buf.data(), buf, HEADERSIZE);
        _buffers = std::move(*buffers);
        std::vector<asio::const_buffer> ioBuf;
        ioBuf.emplace_back(_buf.data(), HEADERSIZE);
        for (auto& i: _buffers) {
            ioBuf.emplace_back(i.first, i.second);
        }
        asyncSendMessage(ioBuf, data, callback);
    }

private:
    void asyncGetHeader();
    void asyncGetMessage();
    void asyncQueueMessage();
    void asyncSocketError(std::error_code ec);
    void asyncSocketShutdownRemove();
    bool doClose() { return _closeOnComplete; }

    ConnStats _stats;
    asio::ip::tcp::socket _socket;
    ConnectionId _connectionId;
    Connections* const _owner;
    //TODO: Might have to turn this into a char*, currently trying to back Message with _freeIt = false
    std::vector _buf;
    BufferSet _buffers;
    std::unique_ptr<ServiceContext::UniqueClient> _clientInfo;
    //TODO: Turn this into state and verify it's correct at all stages
    std::atomic<bool> _closeOnComplete{};
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
MONGO_ALIGN_TO_CACHE class Connections {
    MONGO_DISALLOW_COPYING(Connections);
public:
    Connections(NetworkServer* const server) : _server (server) { }
    void newConnHandler(asio::ip::tcp::socket&& socket);
    //Passing message, which shouldn't allocate any buffers
    void newMessageHandler(NewMessageEventData message);
    const ConnStats& getStats() const { return _stats; }

private:
    friend class AsyncClientConnection;
    ConnStats _stats;
    //TODO: more concurrent
    std::unordered_map<AsyncClientConnection*, std::unique_ptr<AsyncClientConnection>> _conns;
    uint64_t _connectionCount{};
    std::mutex _mutex; //Protects access to _conns and _connectionCount
    NetworkServer* const _server;
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

    void newMessageHandler(NewMessageEventData message);

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

#endif /* SRC_MONGO_UTIL_NET_NETWORK_SERVER_H_ */
