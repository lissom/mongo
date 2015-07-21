/*
 * client_async_message_port.h
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/abstract_operation_executor.h"
#include "async_client_message_port_pool.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
namespace network {

//TODO: Release the _runner after send
class AsyncClientMessagePort final : public AsyncMessagePort {
public:
    using PersistantState = ServiceContext::UniqueClient;

    MONGO_DISALLOW_COPYING(AsyncClientMessagePort);

    AsyncClientMessagePort(AsyncClientMessagePortPool* const owner, asio::ip::tcp::socket socket);
    ~AsyncClientMessagePort() { /* All tear should have already taken place */ };
    void initialize(asio::ip::tcp::socket&& socket) override;
    void retire() override;

    //Stores the opRunner, nothing is done to it
    void setOpRunner(std::unique_ptr<AbstractOperationExecutor> newOpRunner);
    // Deletes the opRunner
    void opRunnerComplete();

    Client* client() {
        return _persistantState.get();
    }

    void persistClientState() {
        fassert(-34, _persistantState.get() == nullptr);
        _persistantState = persist::releaseClient();
        fassert(-35, _persistantState.get() != nullptr);
        fassert(-36, !haveClient());

    }
    void restoreClientState() {
        restoreThreadName();
        fassert(668, _persistantState.get());
        persist::setClient(std::move(_persistantState));
        //Set the mongo thread name, not the setThreadName function here
    }

    // Waiting for async operations to complete
    bool readyToRun() {
        return client() != nullptr;
    }

protected:
    void asyncErrorSend() override;
    void asyncErrorReceive() override;

private:
    void rawInit();
    void asyncDoneReceievedMessage() override;
    void asyncDoneSendMessage() override;

    AsyncClientMessagePortPool* const _owner;
    PersistantState _persistantState;
    std::unique_ptr<AbstractOperationExecutor> _runner;
    size_t _clientWaitFails{};
};
} /* namespace network */
} /* namespace mongo */
