/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/db/dbmessage.h"
#include "mongo/s/operation_runner.h"
#include "mongo/util/log.h"

namespace mongo {

OperationRunner::OperationRunner(network::AsyncClientConnection* const connInfo) :
    port(connInfo),
    message(static_cast<void *>(connInfo->getBuffer()), false),
    request(message, &port) {
}

namespace {
BSONObj buildErrReply( const DBException& ex ) {
    BSONObjBuilder errB;
    errB.append( "$err", ex.what() );
    errB.append( "code", ex.getCode() );
    if ( !ex._shard.empty() ) {
        errB.append( "shard", ex._shard );
    }
    return errB.obj();
}
}

void OperationRunner::run() {
    std::string fullDesc = str::stream() << "Conn" << port.connectionId();
    setThreadName(fullDesc.c_str());

    // Create the client obj, attach to thread
    currentClient.getMake()->get() = service->makeClient(fullDesc, mp);

    processRequest();
    onContextEnd();
}

void OperationRunner::processRequest() {
    try {
        request.process();
    }
    catch ( const AssertionException& ex ) {

        LOG( ex.isUserAssertion() ? 1 : 0 ) << "Assertion failed"
            << " while processing " << opToString( message.operation() ) << " op"
            << " for " << request.getns() << causedBy( ex );

        if ( request.expectResponse() ) {
            message.header().setId(request.id());
            replyToQuery( ResultFlag_ErrSet, &port , message , buildErrReply( ex ) );
        }

        // We *always* populate the last error for now
        LastError::get(cc()).setLastError(ex.getCode(), ex.what());
    }
    catch ( const DBException& ex ) {

        log() << "Exception thrown"
              << " while processing " << opToString( message.operation() ) << " op"
              << " for " << request.getns() << causedBy( ex );

        if ( request.expectResponse() ) {
            message.header().setId(request.id());
            replyToQuery( ResultFlag_ErrSet, &port , message , buildErrReply( ex ) );
        }

        // We *always* populate the last error for now
        LastError::get(cc()).setLastError(ex.getCode(), ex.what());
    }
}

void OperationRunner::onContextStart() {
    currentClient.reset(_client.release());
    std::string fullDesc = str::stream() << "Conn" << port.connectionId();
    setThreadName(fullDesc.c_str());
}

void OperationRunner::onContextEnd() {
    _client.reset(currentClient.release());
}

} // namespace mongo
