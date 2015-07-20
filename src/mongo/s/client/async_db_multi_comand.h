/*
 * async_db_multi_comand.h
 *
 *  Created on: Jul 19, 2015
 *      Author: charlie
 */

#pragma once

#include "multi_command_dispatch.h"

namespace mongo {

class AsyncDBMultiComand: public MultiCommandDispatch {
public:
    AsyncDBMultiComand();
    virtual ~AsyncDBMultiComand();
};

} /* namespace mongo */
