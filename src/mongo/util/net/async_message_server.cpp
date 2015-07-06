/*
 * network_server.cpp
 *
 *  Created on: May 27, 2015
 *      Author: charlie
 */

/*
 * TODO: define ASIO_HAS_MOVE for vendored asio library
 * TODO: -C- handle listen.cpp const Listener* Listener::_timeTracker;
 * TODO: -C- Support IPv6 - requires removing option handling from socks.cpp.  Global opts or net
 * file
 * TODO: -C- Do we care about globalTicketHolder any more?
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include <algorithm>
#include <condition_variable>
#include <errno.h>
#include <thread>
#include <utility>

#include "mongo/base/checked_cast.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/net/async_message_server.h"
#include "mongo/util/net/message.h"

namespace mongo {
namespace network {

const std::string NETWORK_PREFIX = "conn";

AsioAsyncServer::AsioAsyncServer(Options options, AbstractMessagePipeline* const pipeline) :
        _connections(
                new Connections(this,
                        [this](AsyncMessagePort* const port) {this->handlerOperationReady(port);})), _pipeline(
                pipeline), _options(std::move(options)), _timerThread([this] {updateTime();}) {
    // If ipList is specified bind on those ips, otherwise bind to all ips for that port
    if (_options.ipList.size()) {
        const char* start = _options.ipList.c_str();
        asio::ip::address addr;
        while (*start) {
            const char* seperator = strchr(start, ',');
            if (seperator != nullptr) {
                asio::ip::address::from_string(std::string(start, seperator - start).c_str());
                start = seperator + 1;
            } else {
                addr = asio::ip::address::from_string(start);
                start = nullptr;
            }
            _endPoints.emplace_back(_ioService, asio::ip::tcp::endpoint(addr, _options.port));
        }
    } else
        _endPoints.emplace_back(_ioService,
                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), _options.port));
}

AsioAsyncServer::~AsioAsyncServer() {
    _ioService.stop();
    for (auto& t : _threads)
        t.join();
    _timerThread.join();
}

void AsioAsyncServer::handlerOperationReady(AsyncMessagePort* conn) {
    _pipeline->enqueueMessage(checked_cast<network::ClientAsyncMessagePort*>(conn));
}

void AsioAsyncServer::startAllWaits() {
    for (auto& i : _endPoints)
        startWait(&i);
    updateTime();
}

void AsioAsyncServer::startWait(Initiator* const initiator) {
    initiator->_acceptor.async_accept(initiator->_socket, [this, initiator](std::error_code ec) {
        if (!ec) {
            if (!serverGlobalParams.quiet) {
                log() << "connection accepted from " << initiator->_socket.remote_endpoint()
                << std::endl;
            }
            // TOOO: Ensure that move is enabled: ASIO_HAS_MOVE
            // The connection inserts itself into the container
            (void)new ClientAsyncMessagePort(_connections.get(), std::move(initiator->_socket));
        } else {
            // Clear the socket
            asio::ip::tcp::socket sock(std::move(initiator->_socket));
            sock.shutdown(asio::socket_base::shutdown_type::shutdown_both);
            sock.close();
            log() << "Error listening for new connection on "
            << initiator->_acceptor.local_endpoint() << ". Code: " << ec << std::endl;
        }
        // requeue
        startWait(initiator);
    });
}

void AsioAsyncServer::serviceRun() {
    try {
        asio::error_code ec;
        asio::io_service::work work(_ioService);
        _ioService.run(ec);
        if (ec) {
            log() << "Error running service: " << ec << std::endl;
            fassert(-1, !ec);
        }
    } catch (std::exception& e) {
        log() << "Exception running io_service: " << e.what() << std::endl;
        dbexit(EXIT_UNCAUGHT);
    } catch (...) {
        log() << "unknown error running io_service" << std::endl;
        dbexit(EXIT_UNCAUGHT);
    }
}
}  // namespace network
namespace ready {

static std::condition_variable& getReadyCond() {
    static std::condition_variable wait_ready;
    return wait_ready;
}

static std::mutex& getReadyMutex() {
    static std::mutex _mutex;
    return _mutex;
}

static bool _ready { };
static void signalReady() {
    std::lock_guard<std::mutex> lock(getReadyMutex());
    _ready = true;
    getReadyCond().notify_all();
}

}  // namespace ready

namespace network {
void AsioAsyncServer::run() {
    // Init processing of new connections
    int threads = std::thread::hardware_concurrency() / 4;

    try {
        for (; threads > 0; --threads)
            _threads.emplace_back([this] {serviceRun();});

        // Init waiting on the port
        startAllWaits();

        mongo::ready::signalReady();
    } catch (std::exception& e) {
        log() << "can't create new thread for listening, shutting down" << std::endl;
        fassert(-1, false);
    } catch (...) {
        log() << "unknown error accepting new socket" << std::endl;
        dbexit(EXIT_UNCAUGHT);
    }
    serviceRun();  // ensure we don't return until the listening is complete
}

void AsioAsyncServer::updateTime() {
    while (!inShutdown()) {
        const size_t duration = 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(duration));
        clock::incElapsedTimeMillis2(duration);
    }
}

AsioAsyncServer::Initiator::Initiator(asio::io_service& service,
        const asio::ip::tcp::endpoint& endPoint)
try :
        _acceptor(service, endPoint), _socket(service) {
}
catch (std::exception& e) {
    log() << "Unable to create AsioAsyncServer::Initiator::Initiator" << causedBy(e.what())
            << std::endl;
    quickExit(1);
}
} /* namespace network */
} /* namespace mongo */
