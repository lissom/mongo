/*
 * clock.h
 *
 *  Created on: Jun 15, 2015
 *      Author: charlie
 */

#pragma once

namespace mongo {
namespace clock {

const long long getElapsedTimeMillis2();
void incElapsedTimeMillis2(const long long inc);

} /* namespace clock */
} /* namespace mongo */
