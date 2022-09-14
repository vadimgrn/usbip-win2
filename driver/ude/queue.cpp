#include "queue.h"
#include "trace.h"
#include "queue.tmh"

#include <usb.h>
#include "vhci.h"

namespace
{

_Function_class_(EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void IoDeviceControl(
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
void IoStop(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ ULONG ActionFlags)
{
        TraceMsg("Queue 0x%p, Request 0x%p ActionFlags %d", Queue, Request, ActionFlags);
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS QueueInitialize(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);

        cfg.PowerManaged = WdfFalse;
        cfg.EvtIoDeviceControl = IoDeviceControl;
        cfg.EvtIoStop = IoStop;

        WDFQUEUE queue;
        if (auto err = WdfIoQueueCreate(vhci, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        if (auto ctx = get_vhci_context(vhci)) {
                ctx->queue = queue;
        }

        return STATUS_SUCCESS;
}
