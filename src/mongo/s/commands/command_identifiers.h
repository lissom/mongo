/*
 * command_identifiers.h
 *
 *  Created on: Jul 11, 2015
 *      Author: charlie
 */

#pragma once

namespace mongo {
/*
 * Don't change these, they are the wire protocol identifiers
 */
const char CMD_BATCH_INSERT[] = "insert";
const char CMD_BATCH_UPDATE[] = "update";
const char CMD_BATCH_DELETE[] = "delete";
} //namespace mongo
