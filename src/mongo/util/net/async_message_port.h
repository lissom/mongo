/*
 * async_messaging_port.h
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#pragma once

#include <algorithm>
#include <asio.hpp>
#include <boost/thread/thread.hpp>
#include <errno.h>
#include <functional>
#include <mutex>
#include <utility>

#include "mongo/platform/platform_specific.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/unbounded_container.h"
#include "mongo/util/concurrency/queue.h"
#include "mongo/util/exit.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"
#include "mongo/util/timer.h"

namespace mongo {
namespace network {

/*
 * We really need a _simple_ buffer type (that other buffers can then derive from)
 *
 * afaict asio leaves state info behind, so it has problems scaling naturally
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
class AsioAsyncServer;

//TODO: Test array& vs individual
//TODO: Move ConnStats into a vector
//TODO: Message passing upwards
struct ConnStats {
//A cache line is 64 bytes, or 8x8 byte numbers
    uint64_t _bytesIn { };
    uint64_t _bytesOut { };
};

/*
 * Functions starting with async return immediately after queueing work
 * _socket is *not* cleaned up properly for the OS, the holder does this
 *
 * Piggyback isn't supported, only appears to be used for mongoD (and isn't part of the abstract)
 *
 */
//TODO: Abstract class to glue AsyncClientConnection and OperationRunner together
//TODO: MONGO_ALIGN_TO_CACHE, mars release date 6.24
class AsyncMessagePort : public AbstractMessagingPort {
public:
    MONGO_DISALLOW_COPYING(AsyncMessagePort);
    using MessageSize = int32_t;

    /*
     * State is what is being waiting on (unless errored or completed)
     * If the socket is in state operation, then an operation runner referring to this socket
     * is actively runner, it is not safe to delete this socket
     * kError signals that it is no longer possible to use the socket successfully
     * kComplete signals that no further operations are expected on the socket, ever
     * kComplete can replace anything, nothing replaces it.
     * Nothing but kComplete can replace kError.
     */
    enum class State {
        kInit, kWait, kReceieve, kSend, kOperation, kError, kComplete
    };
    AsyncMessagePort(Connections* const owner, asio::ip::tcp::socket&& socket);

    ~AsyncMessagePort();

    virtual void initialize(asio::ip::tcp::socket&& socket);
    virtual void retire();

    MsgData::ConstView getMsgData() {
        verify(_buf.data());
        return MsgData::ConstView(_buf.data());
    }

    char* getBuffer() {
        return _buf.data();
    }
    const char* getBuffer() const {
        return _buf.data();
    }
    //Not the message's size, the buffers
    const size_t getBufferSize() const {
        return _buf.size();
    }
    const ConnStats& getStats() const {
        return _stats;
    }

    const Timer& messageTimer() { return _messageTimer; }

    void reply(Message& received, Message& response, MSGID responseToMsgId) final {
        fassert(-1, state() == State::kOperation || state() == State::kError);
        asyncSendStart(response, responseToMsgId);
    }
    void reply(Message& received, Message& response) final {
        fassert(-1, state() == State::kOperation || state() == State::kError);
        asyncSendStart(response, received.header().getId());
    }

    //Preferred functions to use
    void asyncReceiveStart();
    void asyncSendStart(Message& toSend, MSGID responseTo);

    bool stateGood() {
        return isValid(state());
    }

    /*
     * This function ensures there are no outstanding references to the socket
     */
    bool safeToDelete() {
        return state() != State::kOperation;
    }
    /*
     * All of the below function expose implementation details and shouldn't exist
     * Consider returning std::string for error logging, etc.
     */
    //Only used for mongoD and MessagingPort, breaks abstraction so leaving it alone
    HostAndPort remote() const final {
        return HostAndPort(_socket.remote_endpoint().address().to_string(),
                        _socket.remote_endpoint().port());
    }
    //Only used for an error string for sasl logging
    //TODO: fix sasl logging to use a string, but is in flux in other network stuff, wait for stable and replace with is really being asked
    SockAddr localAddr() const final {
        return SockAddr(_socket.local_endpoint().address().to_string().c_str(),
                _socket.local_endpoint().port());
    }

    SockAddr remoteAddr() const final {
        return SockAddr(_socket.remote_endpoint().address().to_string().c_str(),
                _socket.remote_endpoint().port());
    }

    // End AbstractMessagingPort
protected:
    const State state() const { return _state; }
    virtual void asyncDoneReceievedMessage() = 0;
    virtual void asyncDoneSendMessage() = 0;
    virtual void asyncErrorSend() { };
    virtual void asyncErrorReceive() { };
    void complete() { setState(State::kComplete); }
    //Used when returning the connection the pool for instance
    void wait() { setState(State::kWait); }
    const asio::ip::tcp::socket& socket() const { return _socket; }

    Connections* const _owner;

private:
    void rawInit();

    //Send start assumes a synchronous sender that needs to be detached from
    void asyncSendMessage();

    void asyncReceiveHeader();
    void asyncReceiveMessage();
    inline bool asyncStatusCheck(const char* state, const char* desc, const std::error_code ec,
            const size_t lenGot, const size_t lenExpected) {
        if (ec) {
            asyncSocketError(state, ec);
            return false;
        }
        if (lenGot != lenExpected) {
            asyncSizeError(state, desc, lenGot, lenExpected);
            return false;
        }
        return true;
    }
    void asyncSizeError(const char* state, const char* desc, const size_t lenGot,
            const size_t lenExpected);
    void asyncSocketError(const char* state, const std::error_code ec);
    //Deletes this, there must be no re-entry into the class after calling asyncSocketShutdownRemove
    void asyncSocketShutdownRemove();
    void onReceiveError() {
        setState(State::kError);
        asyncErrorReceive();
        asyncSocketShutdownRemove();
    }
    void onSendError() {
        setState(State::kError);
        asyncErrorSend();
        asyncSocketShutdownRemove();
    }



    bool validMsgSize(MessageSize msgSize) {
    	//static_cast signed to unsigned with number < 0 is implementation defined, check > 0
    	return msgSize > 0
    			&& static_cast<size_t>(msgSize) >= HEADERSIZE
                && static_cast<size_t>(msgSize) <= MaxMessageSizeBytes;
    }

    void bytesIn(uint64_t bytesIn) {
        _stats._bytesIn += bytesIn;
    }
    void bytesOut(uint64_t bytesOut) {
        _stats._bytesOut += bytesOut;
    }

    bool isValid(State state) {
        return state != State::kError && state != State::kComplete;
    }

    void setState(State newState);

    ConnStats _stats;
    asio::ip::tcp::socket _socket;
    //TODO: Might have to turn this into a char*, currently trying to back Message with _freeIt = false
    std::vector<char> _buf;
    std::vector<asio::const_buffer> _ioBuf;
    BufferSet _buffers;
    std::atomic<State> _state { State::kInit };
    Timer _messageTimer;
};

/*
 * TODO: NUMA aware handling will be added one day, so NONE of this is static
 * All functions starting with async are calling from async functions, should not
 * take locks if at all possible
 */
//TODO: MONGO_ALIGN_TO_CACHE
class Connections {
public:
	MONGO_DISALLOW_COPYING(Connections);
	using MessageReadyHandler = std::function<void(AsyncMessagePort*)>;
    Connections(AsioAsyncServer* const server, MessageReadyHandler messageReadyHandler) :
            _server(server), _messageReadyHandler(messageReadyHandler) {
    }
    ~Connections();
    void newConnHandler(asio::ip::tcp::socket&& socket);
    //Passing message, which shouldn't allocate any buffers
    void handlerOperationReady(AsyncMessagePort* port);
    bool getCachedConn(AsyncMessagePort** port) {
    	return _freeConns.pop(port);
    }
    const ConnStats& getStats() const {
        return _stats;
    }

private:
    friend class AsyncMessagePort;
    using FreeQueue = ThreadSafeQueue<AsyncMessagePort*>;

    void handlerPortClosed(AsyncMessagePort* port);

    AsioAsyncServer* const _server;
    MessageReadyHandler _messageReadyHandler;
    UnboundedContainer<AsyncMessagePort*> _activeConns;
    FreeQueue _freeConns;
    ConnStats _stats;
};

} //namespace mongo
} //namespace network
