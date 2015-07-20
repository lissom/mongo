/*
 * abstract_cmd_executor.h
 *
 *  Created on: Jul 12, 2015
 *      Author: charlie
 */

#pragma once

#include <memory>

#include "mongo/bson/bsonobj.h"
#include "mongo/s/abstract_operation_executor.h"
#include "mongo/s/async_state.h"
#include "mongo/s/fast_sync_container.h"
#include "mongo/util/factory.h"

namespace mongo {

//TODO: move this into two objects, one for fast single results, one for multi
class AbstractCmdExecutor {
public:
    struct Settings {
        AbstractOperationExecutor* owner;
        OperationContext* operationCtx;
        const std::string& dbName;
        const BSONObj* const cmdObjBson;
        const int options;
        std::string* errorMsg;
        BSONObjBuilder* result;

        Settings(AbstractOperationExecutor* owner__, OperationContext* operationCtx__,
        const std::string& dbName__, const BSONObj* const cmdObjBson__, int options__,
        std::string* errorMsg__, BSONObjBuilder* result__) : owner(owner__),
                operationCtx(operationCtx__), dbName(dbName__), cmdObjBson(cmdObjBson__),
                options(options__), errorMsg(errorMsg__), result(result__) {
        }
    };
    // The executor child may move out of the cmdBson object, not guarantees over ownership are given
    AbstractCmdExecutor(Settings* settings) :
        _owner(settings->owner),
        _operationCtx(settings->operationCtx),
        _dbName(settings->dbName),
        _cmdObjBson(settings->cmdObjBson),
        options(settings->options),
        _errorMsg(settings->errorMsg),
        _result(settings->result) {
    }

    virtual ~AbstractCmdExecutor() {
    }

    bool complete() {
        return _state == AsyncState::State::kComplete || _state == AsyncState::State::kResultsReady;
    }

    /*
     * Does all the common processing and then calls initialize
     */
    void run();

protected:
    virtual void initialize() = 0;
    Client* client() { return _operationCtx->getClient(); }

    FastSyncBSONObjPtr _results;
    AbstractOperationExecutor* _owner;
    OperationContext* _operationCtx;
    const std::string& _dbName;
    // not const in command->run _cmdObjBson may need to lose it's constness.
    const BSONObj* const _cmdObjBson;
    // not const in command->run _options may need to lose it's constness.
    const int options;
    std::string* _errorMsg;
    BSONObjBuilder* _result;
    AsyncState _state;
};

REGISTER_FACTORY_DECLARE(std::unique_ptr, AbstractCmdExecutor, AbstractCmdExecutor::Settings*)

} /* namespace mongo */
