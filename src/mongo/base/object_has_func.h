/*
 * object_has_signature.h
 *
 *  Created on: Jun 18, 2015
 *      Author: charlie
 */

#pragma once

//TODO: Is this defined else where?
struct SfinaeTypes {
    using one = char;
    using two = struct { char arr[2]; };
};

/*
 * Detects if an object has a signature.  "T::" MUST precede the function name
 * For instance to detect if an object has a toString function:
 * OBJECT_HAS_FUNCTION_SIGNATURE(HasToString, T::toString, void(*)(void))
 */
#define OBJECT_HAS_FUNCTION_SIGNATURE(traitName, funcName, funcType) \
template<typename U> \
class traitName : public SfinaeTypes{ \
    template<typename T, T> struct helper; \
    template<typename T> static one check(helper<funcType, &funcName>*); \
    template<typename T> static two check(...); \
public: \
    static constexpr bool value = sizeof(one) == sizeof(check<U>(0)); \
};
