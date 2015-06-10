/*
 * network_server.cpp
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

/*
 * TODO: define ASIO_HAS_MOVE for vendored asio library
 * TODO: -C- handle listen.cpp const Listener* Listener::_timeTracker;
 * TODO: -C- Support IPv6 - requires removing option handling from socks.cpp.  Global opts or net file
 * TODO: -C- Do we care about globalTicketHolder any more?
 */


#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include <algorithm>
#include <boost/thread/thread.hpp>
#include <errno.h>
#include <utility>

#include "mongo/util/assert_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/network_server.h"

namespace mongo {
namespace network {

const std::string NETWORK_PREFIX = "conn";

NetworkServer::NetworkServer(NetworkOptions options, MessagePipeline* const pipeline) :
        _connections(new Connections(this)),
        _pipeline(pipeline),
        _options(std::move(options)) {
    //If ipList is specified bind on those ips, otherwise bind to all ips for that port
    if (_options.ipList.size()) {
        for (auto &i : _options.ipList) {
            //create an end point with address and port number for each ip
            _endPoints.emplace_back(&_service, asio::ip::tcp::endpoint(i, _options.port));
        }
    }
    else
        _endPoints.emplace_back(&_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(),
                _options.port));
}

NetworkServer::~NetworkServer() {
    _service.stop();
    for (auto& t: _threads)
        t.join();
}

void NetworkServer::newMessageHandler(NewMessageEventData message) {
    _pipeline->enqueueMessage(message);
}

void NetworkServer::startAllWaits() {
    for (auto& i : _endPoints)
        startWait(&i);
}

void NetworkServer::startWait(Initiator* const initiator) {
    initiator->_acceptor.async_accept(initiator->_socket, [this, initiator] (std::error_code ec) {
        if (!ec)
            //TOOO: Ensure that move is enabled: ASIO_HAS_MOVE
            _connections->newConnHandler(std::move(initiator->_socket));
        else {
            //Clear the socket just in case
            asio::ip::tcp::socket sock(std::move(initiator->_socket));
            sock.shutdown(asio::socket_base::shutdown_type::shutdown_both);
            sock.close();
            switch (ec) {
            default:
                log() << "Error code: " << ec << " on port " << initiator->_acceptor.local_endpoint()
                        << std::endl;
            }
        }
        //requeue
        startWait(initiator);
    });
}

void NetworkServer::serviceRun() {
    try {
        asio::error_code ec;
        _service.run(ec);
        if (ec) {
            log() << "Error running service: " << ec << std::endl;
            fassert(-1, !ec);
        }
    } catch (std::exception &e) {
        log() << "Exception running io_service: " << e.what() << std::endl;
        dbexit( EXIT_UNCAUGHT );
    } catch (...) {
        log() << "unknown error running io_service" << std::endl;
        dbexit( EXIT_UNCAUGHT );
    }
}

void NetworkServer::run() {
    //Init waiting on the port
    startAllWaits();

    //Init processing of new connections
    int threads = _options.threads ? _options.threads : boost::thread::hardware_concurrency() / 4;

    //Using boost thread as it allows for cross platform stack size reduction
    boost::thread::attributes attrs;
    attrs.set_stack_size(NETWORK_DEFAULT_STACK_SIZE);
    try {
        for(; threads > 0; --threads)
            _threads.emplace_back(attrs, [this]{ serviceRun(); });
    }
    catch (boost::thread_resource_error&) {
        log() << "can't create new thread for listening, shutting down" << std::endl;
        fassert(-1, false);
    }
    catch (...) {
        log() << "unknown error accepting new socket" << std::endl;
        dbexit( EXIT_UNCAUGHT );
    }
}

NetworkServer::Initiator::Initiator(
        asio::io_service& service,
        const asio::ip::tcp::endpoint& endPoint)
        : _acceptor(service, endPoint), _socket(service) {
}

void Connections::newConnHandler(asio::ip::tcp::socket&& socket) {
    std::unique_ptr<AsyncClientConnection> ci(std::move(socket), ++_connectionCount);
    AsyncClientConnection* conn = ci.get();
    std::unique_lock lock(_mutex);
    auto pos = _conns.emplace(ci.get(), ci);
    verify(pos.second);
    (void)pos;
    lock.release();
    conn->asyncReceiveMessage();
}

void Connections::newMessageHandler(NewMessageEventData message) {
    _server->newMessageHandler(this);
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
        _stats._bytesIn += len;
        if (ec) {
            asyncSocketError(ec);
            return;
        }
        if (len != HEADERSIZE) {
            log() << "Error, invalid header size received: " << len << std::endl;
            asyncSocketShutdownRemove(conn);
            return;
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
        LOG(0) << "recv(): message len " << len << " is invalid. "
               << "Min " << HEADERSIZE << " Max: " << MaxMessageSizeBytes;
        //TODO: can we return an error on the socket to the client?
        asyncSocketShutdownRemove(conn);
    }
    _socket.async_receive(asio::buffer(_buf.data() + HEADERSIZE, msgSize - HEADERSIZE),
            [this](std::error_code ec, size_t len) {
        _stats._bytesIn += len;
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

void AsyncClientConnection::asyncSocketError(std::error_code ec) {
    fassert(-1, ec != 0);
    log() << "Error waiting for header " << ec << std::endl;
    asyncSocketShutdownRemove(conn);
}

void AsyncClientConnection::asyncSocketShutdownRemove() {
    _socket.shutdown(asio::socket_base::shutdown_type::shutdown_both);
    _socket.close();
    //Now that the socket's work is done, post to the async work queue to remove it
    _socket.get_io_service().post([this]{
        std::unique_lock lock(_owner->_mutex);
        fassert(-1, _owner->_conns.erase(this) > 0);
        lock.release();
    });
}

} /* namespace network */
} /* namespace mongo */
