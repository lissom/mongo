/*    Copyright 2015 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <type_traits>

/*
 * Will detect normal or static functions because it doesn't check function signature
 * This should be used to detect functions that might be void or have default arguements = void
 */
#define OBJECT_HAS_FUNCTION(traitName, funcName) \
template<typename T> \
class traitName { \
    template<typename U> static std::true_type check(decltype(&U::funcName)); \
    template<typename U> static std::false_type check(...); \
public: \
    static constexpr bool value = decltype(check<T>(0))::value; \
};

OBJECT_HAS_FUNCTION(HasAnyFooFunc, foo)
struct HasFoo { void foo(int, int); };
struct StaticHasFoo { static void foo(int, int); };
struct NoFoo { };
static_assert(HasAnyFooFunc<HasFoo>::value, "Helper failed to detect foo() exists");
static_assert(HasAnyFooFunc<StaticHasFoo>::value, "Helper failed to detect static foo() exist");
static_assert(!HasAnyFooFunc<NoFoo>::value, "Helper failed to detect foo() doesn't exist");
