/*
 * abstract_cmd_executor.cpp
 *
 *  Created on: Jul 12, 2015
 *      Author: charlie
 */

#include "abstract_cmd_executor.h"

namespace mongo {

void AbstractCmdExecutor::run() {
    initialize();
}

} /* namespace mongo */
