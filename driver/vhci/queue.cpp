#include "queue.h"
#include "trace.h"
#include "queue.tmh"

#include <usb.h>

namespace
{

_Function_class_(EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void EvtIoDeviceControl(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode)
{
        TraceDbg("Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d", 
                  Queue, Request, (int) OutputBufferLength, (int) InputBufferLength, IoControlCode);

        WdfRequestComplete(Request, STATUS_SUCCESS);
}

_Function_class_(EVT_WDF_IO_QUEUE_IO_STOP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void EvtIoStop(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ ULONG ActionFlags)
{
        TraceMsg("Queue 0x%p, Request 0x%p ActionFlags %d", Queue, Request, ActionFlags);
}

} // namespace


PAGEABLE NTSTATUS QueueInitialize(_In_ WDFDEVICE Device)
{
        PAGED_CODE();
    
        WDF_IO_QUEUE_CONFIG queueConfig;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

        queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
        queueConfig.EvtIoStop = EvtIoStop;

        WDFQUEUE queue;
        auto status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

        if (!NT_SUCCESS(status)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate failed %!STATUS!", status);
        }

        return status;
}
