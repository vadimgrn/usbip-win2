/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "pageable.h"
#include <wdm.h>

inline LOOKASIDE_LIST_EX workitem_list;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE inline auto init_workitem_list()
{
        PAGED_CODE();
        return ExInitializeLookasideListEx(&workitem_list, nullptr, nullptr, NonPagedPoolNx, 0, IoSizeofWorkItem(), 'WCHV', 0);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline _IO_WORKITEM *alloc_workitem(_In_ void *IoObject)
{
        NT_ASSERT(IoObject);

        if (auto wi = (_IO_WORKITEM*)ExAllocateFromLookasideListEx(&workitem_list)) {
                IoInitializeWorkItem(IoObject, wi);
                return wi;
        }

        return nullptr;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void free(_In_ _IO_WORKITEM *ctx)
{
        if (ctx) {
                IoUninitializeWorkItem(ctx);
                ExFreeToLookasideListEx(&workitem_list, ctx);
        }
}
