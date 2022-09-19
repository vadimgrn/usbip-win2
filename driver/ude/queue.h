/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

namespace usbip
{

struct queue_context
{
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(queue_context, get_queue_context);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_default_queue(_In_ WDFDEVICE vhci);

} // namespace usbip
