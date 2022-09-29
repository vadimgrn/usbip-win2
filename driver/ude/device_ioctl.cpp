/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_ioctl.h"
#include "trace.h"
#include "device_ioctl.tmh"

#include "context.h"
#include "vhci.h"
#include "device.h"

#include <libdrv\dbgcommon.h>

#include <usbioctl.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_udev(_In_ WDFQUEUE queue)
{
        auto endp = *get_queue_ctx(queue);

        auto &ctx = *get_endpoint_ctx(endp);
        NT_ASSERT(ctx.queue == queue);

        return ctx.device;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto submit_urb(_In_ WDFREQUEST request, _In_ UDECXUSBDEVICE)
{
        auto irp = WdfRequestWdmGetIrp(request);
        auto &urb = *static_cast<URB*>(URB_FROM_IRP(irp));

        TraceDbg("Function %d", urb.UrbHeader.Function);
        return STATUS_NOT_IMPLEMENTED;
}

} // namespace


/*
 * IRP_MJ_INTERNAL_DEVICE_CONTROL 
 */
_Function_class_(EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI usbip::device::internal_device_control(
        _In_ WDFQUEUE Queue, 
        _In_ WDFREQUEST Request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode)
{
        auto udev = get_udev(Queue);

        TraceDbg("udev %04x, %s(%#08lX), OutputBufferLength %Iu, InputBufferLength %Iu", 
                  ptr04x(udev), internal_device_control_name(IoControlCode), IoControlCode, 
                  OutputBufferLength, InputBufferLength);

        auto st = STATUS_NOT_SUPPORTED;

        switch (IoControlCode) {
        case IOCTL_INTERNAL_USB_SUBMIT_URB:
                st = submit_urb(Request, udev);
                break;
        }

        TraceDbg("-> %!STATUS!", st);

        if (st != STATUS_PENDING) {
                WdfRequestComplete(Request, st);
        }
}
