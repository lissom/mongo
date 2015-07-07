/*
 * client_async_message_port.h
 *
 *  Created on: Jun 28, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/abstract_operation_runner.h"
#include "mongo/util/net/async_message_port.h"

namespace mongo {
namespace network {

//TODO: Release the _runner after send
class ClientAsyncMessagePort final : public AsyncMessagePort {
public:
    using PersistantState = ServiceContext::UniqueClient;

    MONGO_DISALLOW_COPYING(ClientAsyncMessagePort);

    ClientAsyncMessagePort(Connections* const owner, asio::ip::tcp::socket socket);
    ~ClientAsyncMessagePort();

    //Stores the opRunner, nothing is done to it
    void setOpRunner(std::unique_ptr<AbstractOperationRunner> newOpRunner);
    // Deletes the opRunner
    void opRunnerComplete();

    Client* clientInfo() {
        return _persistantState.get();
    }

    void persistClientState() {
        _persistantState = std::move(persist::releaseClient());
        invariant(_persistantState.get() != nullptr);
    }
    void restoreClientState() {
        persist::setClient(std::move(_persistantState));
        //Set the mongo thread name, not the setThreadName function here
        mongo::setThreadName(_threadName);
    }
    /*
     * Preserves the legacy logging method, probably need something else like [thread.op#]
     */
    const std::string& threadName() const {
        return _threadName;
    }
    void setThreadName(const std::string& threadName) {
        verify(_threadName.empty() == true);
        _threadName = threadName;
    }


private:
    void asyncDoneReceievedMessage() override;
    void asyncDoneSendMessage() override;

    PersistantState _persistantState;
    std::string _threadName;
    std::unique_ptr<AbstractOperationRunner> _runner;
};
} /* namespace network */
} /* namespace mongo */
