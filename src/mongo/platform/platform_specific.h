/*
 * platform_specific.h
 *
 *  Created on: Jun 1, 2015
 *      Author: charlie
 */

#pragma once

//Generally the first entry of grep . /sys/devices/system/cpu/cpu0/cache/index*/*
// /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size:64
//Need 128 for all platforms, not sure of the spread
const size_t MONGO_ALIGN_TO_CACHE_SIZE = 64;
