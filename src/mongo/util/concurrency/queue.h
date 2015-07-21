/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <mutex>
#include <queue>
#include <thread>

#include "mongo/base/disallow_copying.h"

/**
 * TODO: better concurrency model
 * Managers pointers to a resource
 */
template<typename T>
class ThreadSafeQueue {
public:
	using Container = std::queue<T>;
    MONGO_DISALLOW_COPYING(ThreadSafeQueue);

    ThreadSafeQueue() = default;

	bool pop(T* t) {
		std::unique_lock<std::mutex> lock(_mutex);
		bool haveElem = !_queue.empty();
		if (!haveElem)
			return false;
		*t = std::move(_queue.front());
		_queue.pop();
		return true;
	}

	//Remove the pointer, a raw pointer is enough
	template <typename... Args>
	void emplace(Args... args) {
	    //TODO: Construct outside of the lock?
		std::unique_lock<std::mutex> lock(_mutex);
		_queue.emplace(args...);
	}

	void swap(Container* other) {
		std::unique_lock<std::mutex> lock(_mutex);
		using std::swap;
		swap(_queue, *other);
	}

private:
	std::mutex _mutex;
	Container _queue;
};
