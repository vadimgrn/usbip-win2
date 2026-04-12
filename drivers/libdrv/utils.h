/*
* Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

constexpr void swap(auto &a, auto &b)
{
        decltype(a) tmp(a); // std::move is not available
        a = b;
        b = tmp;
}
