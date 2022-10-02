/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_queue.h"
#include "trace.h"
#include "device_queue.tmh"

#include "context.h"

namespace
{

using namespace usbip;

_Function_class_(EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI canceled_on_queue(_In_ WDFQUEUE queue, _In_ WDFREQUEST request)
{
        TraceDbg("queue %04x, request %04x", ptr04x(queue), ptr04x(request));
        WdfRequestComplete(request, STATUS_CANCELLED);
}


} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create_queue(_In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchManual);
        cfg.EvtIoCanceledOnQueue = canceled_on_queue;
        cfg.PowerManaged = WdfFalse;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
        attrs.EvtCleanupCallback = [] (auto obj) { TraceDbg("Queue %04x cleanup", ptr04x(obj)); };
        attrs.SynchronizationScope = WdfSynchronizationScopeQueue;
        attrs.ParentObject = dev;

        auto &ctx = *get_device_ctx(dev);

        if (auto err = WdfIoQueueCreate(ctx.vhci, &cfg, &attrs, &ctx.queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        TraceDbg("dev %04x, queue %04x", ptr04x(dev), ptr04x(ctx.queue));
        return STATUS_SUCCESS;
}
