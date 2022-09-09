#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

struct queue_context
{
    ULONG PrivateDeviceData;  // just a placeholder
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(queue_context, QueueGetContext)

PAGEABLE NTSTATUS QueueInitialize(_In_ WDFDEVICE Device);
