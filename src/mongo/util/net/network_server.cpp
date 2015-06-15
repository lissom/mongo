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
#include <condition_variable>
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

NetworkServer::NetworkServer(NetworkOptions options, AbstractMessagePipeline* const pipeline) :
        _connections(new Connections(this)),
        _pipeline(pipeline),
        _options(std::move(options)),
        _timerThread([this]{ updateTime(); }) {
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
            _endPoints.emplace_back(_ioService, asio::ip::tcp::endpoint(addr, _options.port));
        }
    }
    else
        _endPoints.emplace_back(_ioService, asio::ip::tcp::endpoint(asio::ip::tcp::v4(),
                _options.port));
}

NetworkServer::~NetworkServer() {
    _ioService.stop();
    for (auto& t: _threads)
        t.join();
    _timerThread.join();
}

void NetworkServer::handlerOperationReady(AsyncClientConnection* conn) {
    _pipeline->enqueueMessage(conn);
}

void NetworkServer::startAllWaits() {
    for (auto& i : _endPoints)
        startWait(&i);
    updateTime();
}

void NetworkServer::startWait(Initiator* const initiator) {
    initiator->_acceptor.async_accept(initiator->_socket, [this, initiator] (std::error_code ec) {
        if (!ec)
            //TOOO: Ensure that move is enabled: ASIO_HAS_MOVE
            _connections->newConnHandler(std::move(initiator->_socket));
        else {
            //Clear the socket
            asio::ip::tcp::socket sock(std::move(initiator->_socket));
            sock.shutdown(asio::socket_base::shutdown_type::shutdown_both);
            sock.close();
            log() << "Error listening for new connection. Code: " << ec
                    //TODO: add template to logstream_builder.h to take operator<< if it exists...
                    //" on port " <<
                    //initiator->_acceptor.local_endpoint()
                    << std::endl;
        }
        //requeue
        startWait(initiator);
    });
}

void NetworkServer::serviceRun() {
    try {
        asio::error_code ec;
        _ioService.run(ec);
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
} //namespace network
namespace ready {

static std::condition_variable& getReadyCond() {
    static std::condition_variable wait_ready;
    return wait_ready;
}

static std::mutex& getReadyMutex() {
    static std::mutex _mutex;
    return _mutex;
}

static bool _ready{};
static void signalReady() {
    std::lock_guard<std::mutex> lock(getReadyMutex());
    _ready = true;
    getReadyCond().notify_all();
}

} //namespace ready

namespace network {
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
        mongo::ready::signalReady();
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

void NetworkServer::updateTime() {
    while(!inShutdown()) {
        const size_t duration = 1;
        boost::this_thread::sleep(boost::posix_time::milliseconds(duration));
        clock::incElapsedTimeMillis2(duration);
    }
}

NetworkServer::Initiator::Initiator(
        asio::io_service& service,
        const asio::ip::tcp::endpoint& endPoint)
        : _acceptor(service, endPoint), _socket(service) {
}

} /* namespace network */

bool ifListenerWaitReady2()
{
   std::unique_lock<std::mutex> lock(ready::getReadyMutex());
   ready::getReadyCond().wait(lock, []{ return ready::_ready; });
   return true;
}

} /* namespace mongo */
