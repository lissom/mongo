/*
 * sharded_operation.h
 *
 *  Created on: Jun 9, 2015
 *      Author: charlie
 */

#pragma once

#include "mongo/s/basic_operation_runner.h"

namespace mongo {

//TODO: Add owner and have the runner pop itself on finish
//TODO: MONGO_ALIGN_TO_CACHE
class OperationRunner: public BasicOperationRunner {
};

} // namespace mongo
