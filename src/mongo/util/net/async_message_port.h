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
#include <mutex>
#include <utility>

#include "mongo/db/client.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/platform_specific.h"
#include "mongo/s/abstract_operation_runner.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/unbounded_container.h"
#include "mongo/util/exit.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"

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
 * AsyncClientConnection(ACC) and OperationRunner(OpRunner) are tightly bound
 * ACC starts a receive, then passes itself to a pipeline, which generates an OperationRunner
 * ACC should not go into State::receive with an OperationRunner in existence
 * ACC should not be in State::send without an OperationRUnner active
 * ACC shall delete the OperationRunner at the end of the send and retain any needed client state
 *
 * ACC has no knowledge of how the message was formed, so it has to copy into it's buffer :(
 *
 */
//This class isn't marked final, probably going to derive from it later on
//TODO: Abstract class to glue AsyncClientConnection and OperationRunner together
MONGO_ALIGN_TO_CACHE class AsyncClientConnection final : public AbstractMessagingPort {
MONGO_DISALLOW_COPYING(AsyncClientConnection);
public:
    //State is what is being waiting on (unless errored or completed)
    enum class State {
        init, receieve, send, operation, error, complete
    };
    using PersistantState = ServiceContext::UniqueClient;
    AsyncClientConnection(Connections* const owner, asio::ip::tcp::socket socket,
            ConnectionId connectionId);

    ~AsyncClientConnection();

    void setOpRuner(std::unique_ptr<AbstractOperationRunner> newOpRunner) {
        fassert(-1, _state != State::complete);
        _runner = std::move(newOpRunner);
    }

    MsgData::View& getMsgData() {
        verify(_buf.data());
        return reinterpret_cast<MsgData::View&>(*_buf.data());
    }

    void closeOnComplete() {
        _closeOnComplete = true;
    }
    PersistantState* const getPersistantState() {
        return _persistantState.get();
    }

    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
    //May need to rexamine this choice later, not sure if async will allow the release
    //Does not store the thread name as this is a const
    void persistClientState() {
        _persistantState.reset(persist::releaseClient());
    }

    void restoreClientState() {
        persist::setClient(_persistantState.release());
        //Set the mongo thread name, not the setThreadName function here
        mongo::setThreadName(_threadName);
    }

    const std::string& threadName() const {
        return _threadName;
    }
    void setThreadName(const std::string& threadName) {
        verify(_threadName.empty() == true);
        _threadName = threadName;
    }

    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
    char* getBuffer() {
        return _buf.data();
    }
    //Not the message's size, the buffers
    size_t getBufferSize() {
        return _buf.size();
    }
    const ConnStats& getStats() const {
        return _stats;
    }
    ConnectionId getConnectionId() {
        return _connectionId;
    }

    // Begin AbstractMessagingPort

    void reply(Message& received, Message& response, MSGID responseToId) final {
        fassert(-1, state() == State::operation);
        SendStart(received, responseToId);
    }
    void reply(Message& received, Message& response) final {
        fassert(-1, state() == State::operation);
        SendStart(received, response.header().getId());
    }

    bool stateGood() {
        return isValid(state());
    }
    bool safeToDelete() {
        return state() != State::operation;
    }
    /*
     * All of the below function expose implementation details and shouldn't exist
     * Consider returning std::string for error logging, etc.
     */
    //Only used for mongoD and MessagingPort, breaks abstraction so leaving it alone
    HostAndPort remote() const final {
        fassert(-2, false);
        return HostAndPort();
    }
    //Only used for an error string for sasl logging
    //TODO: fix sasl logging to use a string
    std::string localAddrString() const final {
        std::stringstream ss;
        ss << _socket.local_endpoint();
        return ss.str();
    }

    std::string remoteAddrString() {
        std::stringstream ss;
        ss << _socket.remote_endpoint();
        return ss.str();
    }

    // End AbstractMessagingPort

private:
    //Send start assumes a synchronous sender that needs to be detached from
    void SendStart(Message& toSend, MSGID responseTo);
    void asyncSendMessage();
    void asyncSendComplete();

    void asyncReceiveStart();
    void asyncReceiveHeader();
    void asyncReceiveMessage();
    void asyncQueueForOperation();
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
    void asyncSocketShutdownRemove();bool doClose() {
        return _closeOnComplete;
    }

    void bytesIn(uint64_t bytesIn) {
        _stats._bytesIn += bytesIn;
    }
    void bytesOut(uint64_t bytesOut) {
        _stats._bytesOut += bytesOut;
    }

    bool isValid(State state) {
        return state != State::error && state != State::complete;
    }
    State state() {
        return _state;
    }
    void setState(State newState);

    Connections* const _owner;
    ConnStats _stats;
    asio::ip::tcp::socket _socket;
    ConnectionId _connectionId;
    //TODO: Might have to turn this into a char*, currently trying to back Message with _freeIt = false
    std::vector<char> _buf;
    std::vector<asio::const_buffer> _ioBuf;
    BufferSet _buffers;
    //Not sure this value is safe to non-barrier
    std::unique_ptr<PersistantState> _persistantState;
    std::string _threadName;
    //TODO: Turn this into state and verify it's correct at all stages
    std::atomic<bool> _closeOnComplete { };
    std::atomic<State> _state { State::init };
    std::unique_ptr<AbstractOperationRunner> _runner { };
};

/*
 * TODO: NUMA aware handling will be added one day, so NONE of this is static
 * All funcions starting with async are calling from async functions, should not
 * take locks if at all possible
 */
MONGO_ALIGN_TO_CACHE class Connections {
MONGO_DISALLOW_COPYING(Connections);
public:
    Connections(AsioAsyncServer* const server) :
            _server(server) {
    }
    void newConnHandler(asio::ip::tcp::socket&& socket);
    //Passing message, which shouldn't allocate any buffers
    void handlerOperationReady(AsyncClientConnection* conn);
    const ConnStats& getStats() const {
        return _stats;
    }

private:
    friend class AsyncClientConnection;
    using ConnectionHolder = UnboundedContainer<network::AsyncClientConnection*>;

    AsioAsyncServer* const _server;
    ConnectionHolder _conns;
    ConnStats _stats;
    //TODO: more concurrent
    uint64_t _connectionCount { };
};

} //namespace mongo
} //namespace network
