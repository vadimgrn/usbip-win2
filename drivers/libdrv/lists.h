/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace libdrv
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto empty(_In_ SLIST_HEADER *head)
{
        return !FirstEntrySList(head);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
SLIST_ENTRY *reverse(_In_opt_ SLIST_ENTRY *head);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto is_zeroed(_In_ const LIST_ENTRY &e)
{
        return !(e.Flink || e.Blink);
}
static_assert(is_zeroed(LIST_ENTRY{}));

} // namespace libdrv
