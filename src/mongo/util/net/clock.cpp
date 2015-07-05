/*
 * clock.cpp
 *
 *  Created on: Jun 15, 2015
 *      Author: charlie
 */

#include <atomic>

#include "mongo/util/net/clock.h"

namespace mongo {
namespace clock {

static std::atomic<long long> elapsedMillis { };

const long long getElapsedTimeMillis2() {
    return elapsedMillis;
}

void incElapsedTimeMillis2(const long long inc) {
    elapsedMillis += inc;
}

} /* namespace clock */
} /* namespace mongo */
