/*
* Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#if defined(__clang__)

#define offsetof_ex(Type, Member)                                      \
        ([]() -> size_t {                                              \
            _Pragma("clang diagnostic push")                           \
            _Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"") \
            return __builtin_offsetof(Type, Member);                   \
            _Pragma("clang diagnostic pop")                            \
        }())

#else // MSVC natively processes offsetof on non-standard layout types in constexpr
  #define offsetof_ex(Type, Member) (__builtin_offsetof(Type, Member))
#endif
