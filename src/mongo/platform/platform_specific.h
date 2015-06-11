/*
 * platform_specific.h
 *
 *  Created on: Jun 1, 2015
 *      Author: charlie
 */

#pragma once

//Generally the first entry of grep . /sys/devices/system/cpu/cpu0/cache/index*/*
// /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size:64
#define MONGO_ALIGN_TO_CACHE
//MONGO_COMPILER_ALIGN_TYPE(64)
//Since C++11 alignas(64)
