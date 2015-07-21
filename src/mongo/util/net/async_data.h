/*
 * async_data.h
 *
 *  Created on: Jul 21, 2015
 *      Author: charlie
 */

#pragma once

#include <system_error>

namespace mongo {
namespace network {

/*
 * Data to send over the wire, this assumes the caller survives the lifetime
 */
// TODO: Replace this with some templates or abstract class and get rid of std::function,
// just wanted one variable to pass for the time being
struct AsyncData {
public:
    using Callback = std::function<char(const bool, char* key, char* returnData,
            size_t returnSize)>;

    AsyncData() { }
    AsyncData(Callback callback, char* data, size_t size) : _callback(callback), _data(data),
            _size(size) { }
    /*
    ~AsyncData() { }
    AsyncData(AsyncData&&) = default;
    AsyncData(const AsyncData&) = default;
    AsyncData& operator=(const AsyncData&) = default;
    AsyncData& operator=(AsyncData&&) = default;
    */

    // Performs the call back, data() is used as the key
    void callback(const bool success, char* data__, size_t size__) {
        _callback(success, _data, data__, size__); }
    char* data() { return _data; }
    size_t size() { return _size; }

private:
    Callback _callback{nullptr};
    // data is returned as the key
    char* _data{nullptr};
    size_t _size{0};
};

} /* namespace network */
} /* namespace mongo */
