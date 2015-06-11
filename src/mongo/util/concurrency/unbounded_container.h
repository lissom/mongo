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
 */
//TODO: better concurrency model
//TODO: better name
//TODO: Specialize for pointers, and unique_ptr
template <typename Key, typename Value>
class UnboundedContainer {
public:
    //Container must be cleaned up, should fix this, but will require specializing
    ~UnboundedContainer() {
        fassert(-1, _container.empty() == true);
    }

    //No checking about decaying into the key b/c the ctor could use it
    template <typename V>
    void set(V&& key, Value&& value) {
        std::unique_lock(_mutex);
        verify(_container.emplace(std::forward<V>(key), std::move(value)).second == true);
    }

    template <typename V>
    void insert(V&& key, std::unique_ptr<Value>&& value) {
        std::unique_lock(_mutex);
        //Use std::move, who knows what get returns exactly
        verify(_container.emplace(std::forward<V>(key), std::move(value.get())).second == true);
    }

    template <typename V>
    void remove(const V& key) {
        std::unique_lock(_mutex);
        _container.erase(key);
    }

    template <typename V>
    Value& at(const V& key) {
        std::unique_lock(_mutex);
        return _container.at(key);
    }

    template <typename V>
    Value& release(const V& key) {
        std::unique_lock(_mutex);
        auto lookup = _container.find(key);
        if(lookup == _container.end())
            throw std::out_of_range("Unable to find key in range");
        Value ret(std::move(*lookup));
        _container.erase(lookup);
        return ret;
    }

private:
    std::mutex _mutex;
    std::unordered_set<Key, Value> _container;
};
