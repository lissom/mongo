/*
 * async_client_connection.cpp
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#include "mongo/util/net/async_cluster_connection.h"
#include "mongo/util/net/async_cluster_end_point_pool.h"

namespace mongo {
namespace network {

// TODO: create a holder for all of this
AsyncClusterConnection::AsyncClusterConnection(ServerConnPool* owner, asio::io_service* ioService) :
    AsyncMessagePort(ioService), _owner(owner) {
}

AsyncClusterConnection::~AsyncClusterConnection() {
}

void AsyncClusterConnection::done() {
    _owner->handlerConnFree(this);
}

void AsyncClusterConnection::asyncDoneReceievedMessage() {
    _asyncData.callback(false, getBuffer(), getBufferSize());
}

void AsyncClusterConnection::asyncDoneSendMessage() {
    //TODO: Check for messages that don't require replies
    asyncReceiveStart();
}

void AsyncClusterConnection::asyncErrorSend() {
    _asyncData.callback(false, nullptr, 0);
}

void AsyncClusterConnection::asyncErrorReceive() {
    //TODO: Should we report failure if the send happened, but receive failed or some indeterminate state
    _asyncData.callback(false, nullptr, 0);
}

void AsyncClusterConnection::connectAndSend(asio::ip::tcp::resolver::iterator&& endPoints,
        AsyncData asyncData) {
    verify(state() == State::kInit);
    verify(endPoints != asio::ip::tcp::resolver::iterator());
    _asyncData = std::move(asyncData);
    connectAndSend(std::move(endPoints));
}

void AsyncClusterConnection::connectAndSend(asio::ip::tcp::resolver::iterator&& endPoints) {
    asio::async_connect(socket(), std::move(endPoints), [this] (const std::error_code& ec,
            asio::ip::tcp::resolver::iterator endPoints) {
        if (!ec) {
            asyncStartSend(_asyncData.data(), _asyncData.size());
        } else if (++endPoints != asio::ip::tcp::resolver::iterator()) {
            connectAndSend(std::move(endPoints));
        } else {
            asyncErrorSend();
        }
    });
}

} /* namespace network */
} /* namespace mongo */
