/*    Copyright 2014 10gen Inc.
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
 * Detects if an object has a signature.
 * Only visible functions can be detected
 * For instance, to detect if an object has a toString function:
 * class MyObj { public: void toString(); };
 * OBJECT_HAS_FUNCTION_SIGNATURE(HasToString, toString, void, void)
 */
#define OBJECT_HAS_FUNCTION_SIGNATURE(traitName, funcName, funcRet, args...) \
template<typename T> \
class traitName { \
    template<typename U, U> struct helper; \
    template<typename U> static std::true_type check(helper<funcRet(U::*)(args), &U::funcName>*); \
    template<typename U> static std::false_type check(...); \
public: \
    static constexpr bool value = decltype(check<T>(0))::value; \
};

OBJECT_HAS_FUNCTION_SIGNATURE(HasFooFunc, foo, void, int, int)
struct HasFoo { void foo(int, int) {}; };
struct NoFoo { };

static_assert(HasFooFunc<HasFoo>::value, "Helper failed to detect foo() exists");
static_assert(!HasFooFunc<NoFoo>::value, "Helper failed to detect foo() doesn't exist");

