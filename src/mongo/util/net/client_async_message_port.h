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
class ClientAsyncMessagePort : public AsyncMessagePort {
public:
    using PersistantState = ServiceContext::UniqueClient;

    MONGO_DISALLOW_COPYING(ClientAsyncMessagePort);

    ClientAsyncMessagePort(Connections* const owner, asio::ip::tcp::socket socket);
    ~ClientAsyncMessagePort();

    //Stores the opRunner, nothing is done to it
    void setOpRunner(std::unique_ptr<AbstractOperationRunner> newOpRunner);
    // Deletes the opRunner
    void opRunnerComplete();

    ConnectionId getConnectionId() {
        return _connectionId;
    }

    Client* clientInfo() {
        return _persistantState.get();
    }
    //In theory this shouldn't be necessary, but using to avoid double deletions if there are errors
    //May need to rexamine this choice later, not sure if async will allow the release
    //Does not store the thread name as this is a const
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
    PersistantState _persistantState;
    std::string _threadName;
    std::unique_ptr<AbstractOperationRunner> _runner;
    const ConnectionId _connectionId;

};

} /* namespace network */
} /* namespace mongo */
