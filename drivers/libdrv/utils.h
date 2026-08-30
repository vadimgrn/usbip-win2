/*
* Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <kernelspecs.h>

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<class T>
constexpr void swap(_Inout_ T &a, _Inout_ T &b)
{
        T tmp(a); // std::move is not available
        a = b;
        b = tmp;
}
