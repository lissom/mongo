/*
 * async_messaging_port.cpp
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/util/log.h"
#include "mongo/util/net/async_message_port.h"
#include "mongo/util/net/async_message_server.h"

namespace mongo {
namespace network {

AsyncMessagePort::AsyncMessagePort(Connections* const owner, asio::ip::tcp::socket socket) :
        _owner(owner), _socket(std::move(socket)), _connectionId(_owner->getNewConnId()),
		_buf(0) {
	_owner->_conns.emplace(this);
    void asyncReceiveStart();
}

AsyncMessagePort::~AsyncMessagePort() {
    //This object should only be destroyed if a runner cannot call back into it
    //Ensure there is no possibility of a _runner that can calling back
    fassert(-1, safeToDelete() == true);
    //TODO: wrap and log
    _socket.shutdown(asio::socket_base::shutdown_type::shutdown_both);
    //TODO: wrap and log
    _socket.close();
    if (!serverGlobalParams.quiet) {
        log() << "end connection " << _socket.remote_endpoint() << std::endl;
    }
}

void AsyncMessagePort::asyncReceiveStart() {
    setState(State::receieve);
    asyncReceiveHeader();
}

void AsyncMessagePort::asyncReceiveHeader() {
    static_assert(NETWORK_MIN_MESSAGE_SIZE > HEADERSIZE, "Min alloc must be > message header size");
    //TODO: capture average message size and use that if > min
    _buf.clear();
    _buf.resize(NETWORK_MIN_MESSAGE_SIZE);
    _socket.async_receive(asio::buffer(_buf.data(), HEADERSIZE),
            [this](const std::error_code& ec, const size_t len) {
                bytesIn(len);
                if (!asyncStatusCheck("receive", "message body", ec, len, getMsgData().getLen()))
                    return;
                //Start the timer as soon as we get a good header so everything is captured
                _messageTimer.reset();
                asyncReceiveMessage();
            });
}

void AsyncMessagePort::asyncReceiveMessage() {
    const auto msgSize = getMsgData().getLen();
    //Forcing into the nearest 1024 size block.  Assuming this was to always hit a tcmalloc size?
    _buf.resize((msgSize + NETWORK_MIN_MESSAGE_SIZE - 1) & 0xfffffc00);
    //Message size may be -1 to check endian?  Not sure if that is current spec
    fassert(-1, msgSize >= 0);
    if (static_cast<size_t>(msgSize) < HEADERSIZE
            || static_cast<size_t>(msgSize) > MaxMessageSizeBytes) {
        log() << "Error during receive: Got an invalid message length in the header( " << msgSize
                << ")" << ". From: " << remoteAddr() << std::endl;
        //TODO: Should we return an error on the socket to the client?
        asyncSocketShutdownRemove();
    }
    _socket.async_receive(asio::buffer(_buf.data() + HEADERSIZE, msgSize - HEADERSIZE),
            [this](const std::error_code& ec, const size_t len) {
                bytesIn(len);
                if (!asyncStatusCheck("receive", "message body", ec, len, getMsgData().getLen()))
                return;
                asyncQueueForOperation();
            });
}

void AsyncMessagePort::asyncQueueForOperation() {
    fassert(-1, state() != State::error);
    setState(State::operation);
    _owner->handlerOperationReady(this);
}

void AsyncMessagePort::asyncSendComplete() {
    doClose() ? asyncSocketShutdownRemove() : asyncReceiveStart();
}

void AsyncMessagePort::asyncSizeError(const char* state, const char* desc, const size_t lenGot,
        const size_t lenExpected) {
    log() << "Error during " << state << ": " << desc << " size expected( " << lenExpected
            << ") was not received" << ". Length: " << lenGot << ". Remote: " << remoteAddr()
            << std::endl;
    setState(State::error);
    asyncSocketShutdownRemove();
}

void AsyncMessagePort::asyncSocketError(const char* state, const std::error_code ec) {
    log() << "Socket error during " << state << ".  Code: " << ec << ".  Remote: "
            << remoteAddr() << std::endl;
    setState(State::error);
    asyncSocketShutdownRemove();
}

void AsyncMessagePort::asyncSocketShutdownRemove() {
    _owner->_conns.erase(this);
}

void AsyncMessagePort::SendStart(Message& toSend, MSGID responseTo) {
    log() << "Sending: " << toSend << std::endl;
    //TODO: get rid of nextMessageId, it's a global atomic, crypto seq. per message thread?
    toSend.header().setId(nextMessageId());
    toSend.header().setResponseTo(responseTo);
    fassert(-3, toSend.buf() != _buf.data());
    size_t size(toSend.header().getLen());
    _buf.resize(size);
    //mongoS should only need single view
    memcpy(_buf.data(), toSend.singleData().data(), size);
    //No more interaction with the message is required at this point
    asyncSendMessage();
}

void AsyncMessagePort::asyncSendMessage() {
    size_t size = getMsgData().getLen();
    _socket.async_send(asio::buffer(_buf.data(), size),
            [this, size] (const std::error_code& ec, const size_t len) {
                if (!asyncStatusCheck("send", "message body", ec, len, size))
                return;
                asyncSendComplete();
            });
}

void AsyncMessagePort::setState(State newState) {
    State currentState = _state;
    do {
        if (currentState == State::complete
                || (currentState == State::error && newState != State::complete))
            return;
        //If the state moves to error or complete stop attempting the change
    } while (!_state.compare_exchange_weak(currentState, newState));
}

void Connections::handlerOperationReady(AsyncMessagePort* conn) {
    _messageReadyHandler(conn);
}

} //namespace mongo
} //namespace network
