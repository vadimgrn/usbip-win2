/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "lists.h"

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
SLIST_ENTRY* libdrv::reverse(_In_opt_ SLIST_ENTRY *head)
{
        SLIST_ENTRY *prev{};

        for (auto cur = head; cur; ) {
                auto next = cur->Next;
                cur->Next = prev;
                prev = cur;
                cur = next;
        }

        return prev;
}
