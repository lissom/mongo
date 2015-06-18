/*
 * unbounded_container.h
 *
 *  Created on: Jun 11, 2015
 *      Author: charlie
 */

#pragma once

#include <unordered_set>
#include <mutex>

#include "mongo/util/assert_util.h"
/*
 * Retains ownership over an unbounded number of objects
 * The name is the objective, to efficient store a potentially unbounded # of elements concurrently
 * Should be specialized for raw pointers
 * Assuming heavy writes, few readers (so mutex is used)j
 */
//TODO: better concurrency model
//TODO: better name
//TODO: Specialize for pointers, and unique_ptr
//Right only pointers, and we own it
template<typename Value>
class UnboundedContainer {
    static_assert(std::is_pointer<Value>::value,
            "This container is only setup to use pointers");
    using Mutex = std::mutex;
    using UniqueLock = std::unique_lock<Mutex>;
public:
    UnboundedContainer() {
    }
    //TODO: Implement a delete policy, needed for other uses
    ~UnboundedContainer() {
    }

    //Cannot be moved or copied
    UnboundedContainer(const UnboundedContainer&) = delete;
    UnboundedContainer(UnboundedContainer&&) = delete;
    UnboundedContainer& operator=(const UnboundedContainer&) = delete;
    UnboundedContainer& operator=(UnboundedContainer&&) = delete;

    void emplace(Value value) {
        UniqueLock lock(_mutex);
        verify(_container.emplace(value).second == true);
    }

    void erase(Value key) {
        UniqueLock lock(_mutex);
        _container.erase(key);
        //Delete may be expensive, drop the lock
        lock.release();
        delete key;
    }

    void release(const Value key) {
        _container.erase(key);
    }

private:
    std::mutex _mutex;
    std::unordered_set<Value> _container;
};
