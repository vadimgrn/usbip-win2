/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

namespace usbip
{

constexpr int from_hex(auto ch) noexcept
{
        if (ch >= '0' && ch <= '9') {
                return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
                return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
                return ch - 'A' + 10;
        }
        return -1;
}

static_assert(from_hex('0') == 0);
static_assert(from_hex('9') == 9);
static_assert(from_hex('a') == 0xa);
static_assert(from_hex('f') == 0xf);
static_assert(from_hex('A') == 0xA);
static_assert(from_hex('F') == 0xF);
static_assert(from_hex('x') == -1);
static_assert(from_hex(L'0') == 0);
static_assert(from_hex(L'f') == 0xf);
static_assert(from_hex(L'x') == -1);

} // namespace usbip
