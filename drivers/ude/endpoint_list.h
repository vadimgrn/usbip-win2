/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "context.h"

namespace usbip
{

struct endpoint_search
{
        endpoint_search(USBD_PIPE_HANDLE h) : handle(h), what(HANDLE) { NT_ASSERT(handle); }
        
        endpoint_search(UINT8 addr) : 
                handle(reinterpret_cast<USBD_PIPE_HANDLE>(static_cast<uintptr_t>(addr))), // for operator bool correctness
                what(ADDRESS) { NT_ASSERT(address == addr); }

        explicit operator bool() const { return handle; }; // largest in union
        auto operator !() const { return !handle; }

        union {
                USBD_PIPE_HANDLE handle;
                UINT8 address;
        };

        enum what_t { HANDLE, ADDRESS };
        what_t what; // union's member selector
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void insert_endpoint_list(_In_ endpoint_ctx &endp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void remove_endpoint_list(_In_ endpoint_ctx &endp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
endpoint_ctx *find_endpoint(_In_ device_ctx &dev, _In_ const endpoint_search &crit);

} // namespace usbip
