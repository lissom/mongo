#pragma once

/*
 * Detects if an object has a signature.
 * "T::" MUST precede the function name, if a function takes void that must be declared
 * Only visible functions can be detected
 * For instance, to detect if an object has a toString function:
 * class MyObj { public: void toString(); };
 * OBJECT_HAS_FUNCTION_SIGNATURE(HasToString, T::toString, void, void)
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
