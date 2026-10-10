/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <stddef.h>

#if defined(__clang__)

#define offsetof_ex(Type, Member)                                       \
        ([]() -> size_t {                                               \
                _Pragma("clang diagnostic push")                        \
                _Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"") \
                return __builtin_offsetof(Type, Member);                \
                _Pragma("clang diagnostic pop")                         \
        }())

#else // MSVC natively processes offsetof on non-standard layout types in constexpr
  #define offsetof_ex(Type, Member) (__builtin_offsetof(Type, Member))
#endif

#define offset_plus_size(Type, Member) (offsetof_ex(Type, Member) + sizeof(((Type*)nullptr)->Member))


namespace usbip::impl
{

template<typename>
struct anysize_array_traits;

template<typename Elem, size_t N>
struct anysize_array_traits<Elem[N]>
{
        static_assert(N == 1); // ANYSIZE_ARRAY
        static constexpr size_t elem_size = sizeof(Elem);
};

} // namespace usbip::impl


/**
 * Computes the byte size of a Windows-style variable-length structure whose
 * trailing array member is declared with ANYSIZE_ARRAY (1 element).
 *
 * Validates at compile time that Type::Member is a 1-element array (rejecting
 * pointers and fixed-size arrays with N != 1), evaluates Count once in size_t,
 * and returns at least sizeof(Type) when Count <= 1.
 *
 * @param Type   structure type containing the trailing ANYSIZE_ARRAY member
 * @param Member name of the trailing array member (declared as T Member[ANYSIZE_ARRAY])
 * @param Count  number of elements in Member
 * @return total byte size required for Type with Count elements
 */
#define sizeof_anysize_struct(Type, Member, Count) \
        ([] (size_t cnt) constexpr noexcept -> size_t { \
                constexpr auto elem_sz = usbip::impl::anysize_array_traits< \
                        decltype(((Type*)nullptr)->Member)>::elem_size; \
                return sizeof(Type) + (cnt > 1 ? (cnt - 1)*elem_sz : 0); \
        }(Count))
