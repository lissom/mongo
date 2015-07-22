/*
 * async_db_multi_comand.h
 *
 *  Created on: Jul 19, 2015
 *      Author: charlie
 */

#pragma once

#include <atomic>

#include "multi_command_dispatch.h"

namespace mongo {

class AsyncDBMultiComand: public MultiCommandDispatch {
public:
    AsyncDBMultiComand();
    virtual ~AsyncDBMultiComand();

    void addCommand(const ConnectionString& endpoint,
                        StringData dbName,
                        const BSONObj& request) override;

    //No op, sending was taken care of with the addCommand
    void sendAll() { };

    int numPending() const {
        return 0;
    };

    Status recvAny(ConnectionString* endpoint, BSONSerializable* response) override;

private:

};

} /* namespace mongo */
