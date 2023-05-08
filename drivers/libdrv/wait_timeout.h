/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

namespace wdm
{

enum wait_unit : LONGLONG
{ 
        _100_nsec = 1,  // in units of 100 nanoseconds
        usec = 10*_100_nsec, // microsecond
        msec = 1000*usec, // millisecond
        second = 1000*msec,
        minute = 60*second,
        hour = 60*minute,
};

enum class period { absolute, relative };
/*
 * Timeout value for KeWaitForSingleObject and other. 
 */
constexpr auto make_timeout(_In_ LONGLONG value, _In_ period type)
{
        static_assert(sizeof(value) == sizeof(LARGE_INTEGER::QuadPart));
        return LARGE_INTEGER{ .QuadPart = type == period::relative ? -value : value};
}

} // namespace wdm
