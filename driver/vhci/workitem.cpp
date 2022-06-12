/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "workitem.h"

LOOKASIDE_LIST_EX workitem_list;

NTSTATUS init_workitem_list()
{
        return ExInitializeLookasideListEx(&workitem_list, nullptr, nullptr, NonPagedPoolNx, 0, 
                                           IoSizeofWorkItem(), 'WCHV', 0);
}

_IO_WORKITEM *alloc_workitem(_In_ void *IoObject)
{
        NT_ASSERT(IoObject);

        if (auto wi = (_IO_WORKITEM*)ExAllocateFromLookasideListEx(&workitem_list)) {
                IoInitializeWorkItem(IoObject, wi);
                return wi;
        }

        return nullptr;
}

void free(_In_ _IO_WORKITEM *ctx)
{
        if (ctx) {
                IoUninitializeWorkItem(ctx);
                ExFreeToLookasideListEx(&workitem_list, ctx);
        }
}
