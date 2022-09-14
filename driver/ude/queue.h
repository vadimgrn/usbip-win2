#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

struct queue_context
{
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(queue_context, get_queue_context);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS QueueInitialize(_In_ WDFDEVICE vhci);
