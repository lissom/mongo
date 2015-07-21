/*
 * async_client_connection.h
 *
 *  Created on: Jul 20, 2015
 *      Author: charlie
 */

#pragma once

#include <memory>

#include "mongo/client/connection_string.h"
#include "mongo/rpc/protocol.h"
#include "mongo/util/net/async_data.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
namespace network {

class ServerConnPool;
//TODO: change the buffer in async message port to void* so we don't have to copy
class AsyncClusterConnection final : public AsyncMessagePort {
public:
    AsyncClusterConnection(ServerConnPool* owner, asio::io_service* ioService);
    virtual ~AsyncClusterConnection();

    // Can only be called when the socket it initializing currently
    void connectAndSend(asio::ip::tcp::resolver::iterator&& endPoints,
            AsyncData asyncData);

    void done();

private:
    void asyncDoneReceievedMessage() override;
    void asyncDoneSendMessage() override;
    void asyncErrorSend() override;
    void asyncErrorReceive() override;

    void connectAndSend(asio::ip::tcp::resolver::iterator&& endPoints);


    AsyncData _asyncData;
    ServerConnPool* _owner;

    // TODO: actually send the isMaster command
    rpc::ProtocolSet _serverProtocols{rpc::supports::kAll};
    rpc::ProtocolSet _clientProtocols{rpc::supports::kAll};
};

} /* namespace network */
} /* namespace mongo */

