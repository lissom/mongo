/*
 * abstract_operation_runner.h
 *
 *  Created on: Jun 13, 2015
 *      Author: charlie
 */

#pragma once

namespace mongo {

class AbstractOperationRunner {
public:
    MONGO_DISALLOW_COPYING(AbstractOperationRunner);
    virtual ~AbstractOperationRunner() { }
};

} //namespace mongo
