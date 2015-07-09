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

std::atomic<uint64_t> connectionCount{};

AsyncMessagePort::AsyncMessagePort(Connections* const owner, asio::ip::tcp::socket&& socket) :
        _owner(owner), _socket(std::move(socket)), _buf(0) {
	rawInit();
}

AsyncMessagePort::~AsyncMessagePort() {
    //This object should only be destroyed if a runner cannot call back into it
    //Ensure there is no possibility of a _runner that can calling back
    fassert(-6, safeToDelete() == true);
}

void AsyncMessagePort::initialize(asio::ip::tcp::socket&& socket) {
	_socket = std::move(socket);
	rawInit();
}

void AsyncMessagePort::rawInit() {
    _networkMessageTimer.reset();
	setConnectionId(++connectionCount);
	_state = State::kInit;
	_owner->_activeConns.insert(this);
}

void AsyncMessagePort::retire() {
    //This object should only be destroyed if a runner cannot call back into it
    //Ensure there is no possibility of a _runner that can calling back
    fassert(-7, safeToDelete() == true);

    //TODO: wrap and log
    _socket.shutdown(asio::socket_base::shutdown_type::shutdown_both);
    //TODO: wrap and log
    _socket.close();
    _threadName.insert(0, "retired: ");
    setConnectionId(-1);
}

void AsyncMessagePort::asyncReceiveStart() {
    setState(State::kReceieve);
    asyncReceiveHeader();
}

void AsyncMessagePort::asyncReceiveHeader() {
    static_assert(NETWORK_MIN_MESSAGE_SIZE > HEADERSIZE, "Min alloc must be > message header size");
    _buf.clear();
    _buf.resize(NETWORK_MIN_MESSAGE_SIZE);
    _socket.async_receive(asio::buffer(_buf.data(), HEADERSIZE),
            [this](const std::error_code& ec, const size_t len) {
                bytesIn(len);
                if (!asyncStatusCheck("receive", "message header", ec, len, HEADERSIZE))
                    return onReceiveError();
                //Start the timer as soon as we get a good header so everything is captured
                asyncReceiveMessage();
            });
}

void AsyncMessagePort::asyncReceiveMessage() {
    const auto msgSize = getMsgData().getLen();
    //Forcing into the nearest 1024 size block.  Assuming this was to always hit a tcmalloc size?
    _buf.resize((msgSize + NETWORK_MIN_MESSAGE_SIZE - 1) & 0xfffffc00);
    //Message size may be -1 to check endian
    fassert(-8, msgSize >= 0);
    if (!_socket.is_open())
        log() << "Socket is closed" << std::endl;
    if (!validMsgSize(msgSize)) {
        log() << "Error during receive: Got an invalid message length in the header(" << msgSize
                << ")" << ". From: " << remoteAddr() << std::endl;
        //TODO: Should we return an error on the socket to the client?
        onReceiveError();
    }
    _socket.async_receive(asio::buffer(_buf.data() + HEADERSIZE, msgSize - HEADERSIZE),
            [this](const std::error_code& ec, const size_t len) {
                bytesIn(len);
                if (!asyncStatusCheck("receive", "message body", ec, len, getMsgData().getLen() - HEADERSIZE))
                    return onReceiveError();
                setState(State::kOperation);
                asyncDoneReceievedMessage();
            });
}

void AsyncMessagePort::asyncSizeError(const char* state, const char* desc, const size_t lenGot,
        const size_t lenExpected) {
    log() << "Error during " << state << ": " << desc << " size expected( " << lenExpected
            << ") was not received" << ". Length: " << lenGot << ". Remote: " << remoteAddr()
            << std::endl;
    setState(State::kError);
}

void AsyncMessagePort::asyncSocketError(const char* state, const std::error_code ec) {
    log() << "Socket error during " << state << ".  Code: " << ec << ".  "
            <<" Message: " << ec.message()
            << "Remote: " << remoteAddr()
            << std::endl;
    setState(State::kError);
}

void AsyncMessagePort::asyncSocketShutdownRemove() {
    _owner->handlerPortClosed(this);
}

void AsyncMessagePort::asyncSendStart(Message& toSend, MSGID responseToMsgId) {
    fassert(-3, toSend.buf() != 0);
    //TODO: get rid of nextMessageId, it's a global atomic, crypto seq. per message thread?
    toSend.header().setId(nextMessageId());
    toSend.header().setResponseTo(responseToMsgId);
    //It's possible the buffer we passed was reused, if not use the port's owned buffer
    if (toSend.buf() != _buf.data()) {
		size_t size(toSend.header().getLen());
		_buf.resize(size);
		//mongoS should only need single view
		memcpy(_buf.data(), toSend.singleData().view2ptr(), size);
    }
    //No more interaction with the message is required at this point
    asyncSendMessage();
}

void AsyncMessagePort::asyncSendMessage() {
	fassert(-4, state() != State::kError && state() != State::kComplete);
	setState(State::kSend);
    MessageSize msgSize = getMsgData().getLen();
    fassert(-5, validMsgSize(msgSize));
    _socket.async_send(asio::buffer(_buf.data(), msgSize),
            [this, msgSize] (const std::error_code& ec, const size_t len) {
                if (!asyncStatusCheck("send", "message body", ec, len, msgSize))
                    onSendError();
                setState(State::kOperation);
                asyncDoneSendMessage();
            });
}

void AsyncMessagePort::setState(State newState) {

    State currentState = _state;
    while (currentState != State::kComplete &&
    		!(currentState == State::kError && newState != State::kComplete)) {
        if (_state.compare_exchange_weak(currentState, newState))
        	break;
    }
}

Connections::~Connections() {
	fassert(-6, _activeConns.empty());
	FreeQueue::Container toFree;
	_freeConns.swap(&toFree);
	while(!toFree.empty()) {
		delete toFree.front();
		toFree.pop();
	}
}

void Connections::handlerOperationReady(AsyncMessagePort* conn) {
    _messageReadyHandler(conn);
}

void Connections::handlerPortClosed(AsyncMessagePort* port) {
	_activeConns.erase(port);
	port->retire();
	_freeConns.emplace(std::move(port));
}

} //namespace mongo
} //namespace network
