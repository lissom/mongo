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
        const char* start = _options.ipList.c_str();
        asio::ip::address addr;
        while(*start) {
            const char* seperator = strchr(start, ',');
            if (seperator != nullptr) {
                asio::ip::address::from_string(std::string(start, seperator - start).c_str());
                start = seperator + 1;
            }
            else {
                addr = asio::ip::address::from_string(start);
                start = nullptr;
            }
            _endPoints.emplace_back(_service, asio::ip::tcp::endpoint(addr, _options.port));
        }
    }
    else
        _endPoints.emplace_back(_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(),
                _options.port));
}

NetworkServer::~NetworkServer() {
    _service.stop();
    for (auto& t: _threads)
        t.join();
}

void NetworkServer::newMessageHandler(AsyncClientConnection* conn) {
    _pipeline->enqueueMessage(conn);
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
            std::stringstream ss;
            ss << "Network error. Code: " << ec << " on port " <<
                    initiator->_acceptor.local_endpoint();
            log() << ss.str() << std::endl;
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

} /* namespace network */
} /* namespace mongo */
