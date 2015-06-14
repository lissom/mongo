/*
 * sharded_operation.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/db/client.h"
#include "mongo/db/dbmessage.h"
#include "mongo/s/operation_runner.h"
#include "mongo/util/log.h"
#include "mongo/db/lasterror.h"

namespace mongo {

OperationRunner::OperationRunner(network::AsyncClientConnection* const connInfo) :
    port(connInfo),
    message(static_cast<void *>(connInfo->getBuffer()), false),
    request(message, port) {
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
    verify(_state == State::init);
    try {
        Client::initThread("conn", port);
    }
    catch (std::exception &e) {
        log() << "Failed to initialize operation runner thread specific variables: " << e.what();
        setErrored();
    }
    catch (...) {
        log() << "Failed to initialize operation runner thread specific variables: unknown exception";
        setErrored();
    }
    port->setThreadName(getThreadName());
    setState(State::running);
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
            replyToQuery( ResultFlag_ErrSet, port , message , buildErrReply( ex ) );
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
            replyToQuery( ResultFlag_ErrSet, port , message , buildErrReply( ex ) );
        }

        // We *always* populate the last error for now
        LastError::get(cc()).setLastError(ex.getCode(), ex.what());
    }
}

void OperationRunner::onContextStart() {
    port->restoreClientState();
}

void OperationRunner::onContextEnd() {
    port->persistClientState();
}

} // namespace mongo
