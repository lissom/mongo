/*
 * async_messaging_port.h
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#pragma once

#include <algorithm>
#include <boost/thread/thread.hpp>
#include <errno.h>
#include <utility>

#include "mongo/platform/platform_specific.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"

namespace mongo {
namespace network {

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

//TODO: Test array& vs individual
//TODO: Move ConnStats into a vector
//TODO: Message passing upwards
struct ConnStats {
//A cache line is 64 bytes, or 8x8 byte numbers
    uint64_t _bytesIn{};
    uint64_t _bytesOut{};
};

/*
 * Functions starting with async return immediately after queueing work
 * _socket is *not* cleaned up properly for the OS, the holder does this
 *
 * Piggyback isn't supported, only appears to be used for mongoD
 */
MONGO_ALIGN_TO_CACHE class AsyncClientConnection : public AbstractMessagingPort {
    MONGO_DISALLOW_COPYING(AsyncClientConnection);
public:
    using PersistantState = ServiceContext::UniqueClient;
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
    LastError& getLastError() { return _persistantState; }
    void setPersistantState(PersistantState* state) {
        _persistantState.reset(state);
    }
    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
    PersistantState* releasePersistantState() {
        return _persistantState.release();
    }
    const std::string& threadName() const { return _threadName; }
    std::string& setThreadName(const std::string& threadName) { verify(_threadName.empty() == true); _threadName = threadName; }

    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
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
            bytesOut(len);
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

    // Begin AbstractMessagingPort

    //TODO: Tie reply, async send and callbacks
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
    //Only used for mongoD and MessagingPort, breaks abstraction so leaving it alone
    HostAndPort remote() const final { fassert(-2, false); return SockAddr(); }
    //Only used for an error string for sasl logging
    //TODO: fix sasl logging to use a string
    std::string localAddrString() const final;

    // End AbstractMessagingPort

private:

    void asyncSend(Message& toSend, int responseTo = 0);
    void asyncSendSingle(const Message& toSend);
    void asyncSendMulti(const Message& toSend);

    void asyncGetHeader();
    void asyncGetMessage();
    void asyncQueueMessage();
    void asyncSocketError(std::error_code ec);
    void asyncSocketShutdownRemove();
    bool doClose() { return _closeOnComplete; }

    void bytesIn(uint64_t bytesIn) {
        _stats._bytesIn += bytesIn;
    }
    void bytesOut(uint64_t bytesOut) {
        _stats._bytesOut += bytesOut;
    }

    ConnStats _stats;
    asio::ip::tcp::socket _socket;
    ConnectionId _connectionId;
    Connections* const _owner;
    //TODO: Might have to turn this into a char*, currently trying to back Message with _freeIt = false
    std::vector _buf;
    BufferSet _buffers;
    std::unique_ptr<PersistantState> _persistantState;
    std::string _threadName;
    //TODO: Turn this into state and verify it's correct at all stages
    std::atomic<bool> _closeOnComplete{};
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
    void newMessageHandler(AsyncClientConnection* conn);
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

} //namespace mongo
} //namespace network

