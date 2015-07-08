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
 * Assuming all writes, few readers (so mutex is used)
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
    UnboundedContainer() = default;

    //Cannot be moved or copied
    UnboundedContainer(const UnboundedContainer&) = delete;
    UnboundedContainer(UnboundedContainer&&) = delete;
    UnboundedContainer& operator=(const UnboundedContainer&) = delete;
    UnboundedContainer& operator=(UnboundedContainer&&) = delete;

    void emplace(Value&& value) {
        UniqueLock lock(_mutex);
        verify(_container.emplace(std::move(value)).second == true);
    }

    void insert(Value value) {
        UniqueLock lock(_mutex);
        verify(_container.emplace(std::move(value)).second == true);
    }

    void erase(const Value& key) {
        UniqueLock lock(_mutex);
        _container.erase(key);
    }

    bool empty() const {
    	UniqueLock lock(_mutex);
    	return _container.empty();
    }

private:
    mutable std::mutex _mutex;
    std::unordered_set<Value> _container;
};
