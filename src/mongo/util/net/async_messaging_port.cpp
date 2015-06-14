/*
 * async_messaging_port.cpp
 *
 *  Created on: Jun 6, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/util/net/async_messaging_port.h"
#include "mongo/util/net/network_server.h"

namespace mongo {
namespace network {

void Connections::newConnHandler(asio::ip::tcp::socket&& socket) {
    AsyncClientConnection* conn = new AsyncClientConnection(std::move(socket), _connectionCount);
    _conns.emplace(conn);
    //Ensure the insert happened
    conn->asyncReceiveMessage();
}

void Connections::newMessageHandler(AsyncClientConnection* conn) {
    _server->newMessageHandler(conn);
}

void AsyncClientConnection::asyncReceiveMessage() {
    asyncGetHeader();
}

void AsyncClientConnection::asyncGetHeader() {
    static_assert(NETWORK_MIN_MESSAGE_SIZE > HEADERSIZE, "Min alloc must be > message header size");
    //TODO: capture average message size and use that if > min
    _buf.clear();
    _buf.resize(NETWORK_MIN_MESSAGE_SIZE);
    _socket.async_receive(asio::buffer(_buf.data(), HEADERSIZE),
            [this](std::error_code ec, size_t len) {
        bytesIn(len);
        if (ec)
            return asyncSocketError(ec);
        if (len != HEADERSIZE) {
            log() << "Error, invalid header size received: " << len << std::endl;
            return asyncSocketShutdownRemove();
        }
        asyncGetMessage(conn);
    });
}

void AsyncClientConnection::asyncGetMessage() {
    const auto msgSize = getMsgData().getLen();
    //Forcing into the nearest 1024 size block.  Assuming this was to always hit a tcmalloc size?
    _buf.resize((msgSize + NETWORK_MIN_MESSAGE_SIZE - 1) & 0xfffffc00);
    //Message size may be -1 to check endian?  Not sure if that is current spec
    fassert(-1, msgSize >= 0);
    if ( static_cast<size_t>(msgSize) < HEADERSIZE ||
         static_cast<size_t>(msgSize) > MaxMessageSizeBytes ) {
        log() << "recv(): message len " << len << " is invalid. "
               << "Min " << HEADERSIZE << " Max: " << MaxMessageSizeBytes;
        //TODO: can we return an error on the socket to the client?
        asyncSocketShutdownRemove(conn);
    }
    _socket.async_receive(asio::const_buffer(_buf.data() + HEADERSIZE, msgSize - HEADERSIZE),
            [this](std::error_code ec, size_t len) {
        bytesIn(len);
        if (ec) {
            asyncSocketError(ec);
            return;
        }
        if (len != HEADERSIZE) {
            log() << "Error, invalid header size received: " << len << std::endl;
            asyncSocketShutdownRemove(conn);
            return;
        }
        asyncQueueMessage();
    });
}

void AsyncClientConnection::asyncQueueMessage() {
    _owner->newMessageHandler(this);
}

void AsyncClientConnection::asyncSizeError(const char* desc, size_t size) {
    log() << desc << ". Length: " << len << std::endl;
    asyncSocketShutdownRemove();
}

void AsyncClientConnection::asyncSocketError(std::error_code ec) {
    log() << "Socket error.  Code: " << ec << ".  Remote: " << _socket.remote_endpoint()
            << std::endl;
    asyncSocketShutdownRemove(conn);
}

void AsyncClientConnection::asyncSocketShutdownRemove() {
    _socket.shutdown(asio::socket_base::shutdown_type::shutdown_both);
    _socket.close();
    //Now that the socket's work is done, post to the async work queue to remove it
    _socket.get_io_service().post([this]{
        std::unique_lock lock(_owner->_mutex);
        _owner->_conns.erase(this);
        lock.release();
    });
}

void AsyncClientConnection::asyncSend(Message& toSend, int responseTo) {
    //TODO: get rid of nextMessageId, it's a global atomic, crypto seq. per message thread?
    toSend.header().setId(nextMessageId());
    toSend.header().setResponseTo(responseTo);
    //TODO: Piggyback data is added here, only seems relevant to kill cursor
    toSend.isSingleData() ? asyncSendSingle(toSend) : asyncSendMulti(toSend);
}

void AsyncClientConnection::asyncSendSingle(const Message& toSend)  {
    _socket.async_send(asio::const_buffer(toSend.singleData()));
}

void AsyncClientConnection::asyncSendMulti(const Message& toSend) {

}

} //namespace mongo
} //namespace network
