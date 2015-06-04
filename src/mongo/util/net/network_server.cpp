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
#include "mongo/util/net/network_server.h"



#include "mongo/util/net/message.h"



namespace mongo {
namespace network {

OnConnect OnConnectEvent = nullptr;
const std::string NETWORK_PREFIX = "conn";

NetworkServer::NetworkServer(NetworkOptions options, Connections* connections) :
        _options(std::move(options)),
        _connections(connections) {
    invariant(OnConnectEvent != nullptr);
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

NetworkServer::Initiator::Initiator(
        asio::io_service& service,
        const asio::ip::tcp::endpoint& endPoint)
        : _acceptor(service, endPoint), _socket(service) {
}

void NetworkServer::startAllWaits() {
    for (auto& i : _endPoints)
        startWait(&i);
}

void NetworkServer::startWait(Initiator* const initiator) {
    initiator->_acceptor.async_accept(initiator->_socket, [this, initiator] (std::error_code ec) {
        if (!ec)
            _connections->newConn(std::move(socket));
        else {
            //Clear the socket just in case
            asio::ip::tcp::socket sock(std::move(initiator->_socket));
            (void) sock;
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
        for (int i = 0; i < threads; ++i)
            boost::thread(attrs, [this]{ serviceRun(); });
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

NetworkServer::~NetworkServer() {
    _service.stop();
}


void Connections::newConnHandler(asio::ip::tcp::socket&& socket) {
    std::unique_ptr<ConnectionInfo> ci(std::move(socket), NETWORK_PREFIX + std::to_string(++_connectionCount));
    ConnectionInfo* conn = ci.get();
    std::unique_lock lock(_mutex);
    auto pos = _conns.emplace(ci.get(), ci);
    assert(pos.second);
    (void)pos;
    lock.release();
    GetHeader(conn);
}

void Connections::GetHeader(ConnectionInfo* conn) {
    conn->_buf(sizeof(MSGHEADER::Value));
    asio::buffer b(conn->_buf.data(), conn->_buf.size());
    asio::async_read(b, [this, conn](std::error_code ec, size_t len) {
    })
    if(asio::error_code err = conn->_socket.async_receive(
            asio::buffer(conn->_buf),
            [this, conn] {

    })) {
        log() << "Error waiting for header " << err << std::endl;
        conn->_socket.shutdown(asio::socket_base::shutdown_type::shutdown_both);
        std::unique_lock lock(_mutex);
        auto pos = std::find_if(_conns.begin(), _conns.end(), [conn](const ConnectionInfo &ci)
                ->bool {
            return conn == &ci;
        });
        assert(pos != _conns.end());
        lock.release();

    }
}

void Connections::GetMessage(ConnectionInfo* conn) {

}

/*
 *
//Ensure allocation is a multiple of 1024 and large enough
//Taken from the other networking code in case there are any hidden assumpitons
//Not sure I see the propose given that there are other non-aligned allocs so
int z = (len+1023)&0xfffffc00;

MONGO_ALIGN_TO_CACHE struct ConnStats {
    uint64_t queries{};
    uint64_t inserts{};
    uint64_t updates{};
    uint64_t deletes{};
    uint64_t commands{};
};

    try {

    }
    catch ( AssertionException& e ) {
        log() << "AssertionException handling request, closing client connection: " << e << std::endl;
        portWithHandler->shutdown();
    }
    catch ( SocketException& e ) {
        log() << "SocketException handling request, closing client connection: " << e << std::endl;
        portWithHandler->shutdown();
    }
    catch ( const DBException& e ) { // must be right above std::exception to avoid catching subclasses
        log() << "DBException handling request, closing client connection: " << e << std::endl;
        portWithHandler->shutdown();
    }
    catch ( std::exception &e ) {
        error() << "Uncaught std::exception: " << e.what() << ", terminating" << std::endl;
        dbexit( EXIT_UNCAUGHT );
    }

 */

} /* namespace network */
} /* namespace mongo */
