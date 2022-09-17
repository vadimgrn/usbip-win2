/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "queue.h"
#include "trace.h"
#include "queue.tmh"

#include "driver.h"
#include "vhci.h"
#include "ioctl.h"

#include <libdrv\dbgcommon.h>
#include <usbip\vhci.h>

#include <usb.h>
#include <usbuser.h>

#include <wdfusb.h>
#include <UdeCx.h>

namespace
{


/*
 * IRP_MJ_DEVICE_CONTROL 
 */
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
        if (IoControlCode == IOCTL_USB_USER_REQUEST) {
                USBUSER_REQUEST_HEADER *r{}; 
                if (!WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &static_cast<void*>(r), nullptr)) {
                        TraceDbg("USB_USER_REQUEST, %s(%#08lX), RequestBufferLength %lu", 
                                  usbuser_request_name(r->UsbUserRequest), r->UsbUserRequest, r->RequestBufferLength);
                }
        } else {
                TraceDbg("%s(%#08lX), OutputBufferLength %Iu, InputBufferLength %Iu", 
                          device_control_name(IoControlCode), IoControlCode, OutputBufferLength, InputBufferLength);
        }

        auto complete = true;
        auto st = STATUS_INVALID_DEVICE_REQUEST;

        switch (IoControlCode) {
        case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
                st = get_imported_devices(Request);
                break;
        case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
                st = plugin_hardware(Request);
                break;
        case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
                st = unplug_hardware(Request);
                break;
        default:
                complete = !UdecxWdfDeviceTryHandleUserIoctl(WdfIoQueueGetDevice(Queue), Request);
        }

        if (complete) {
                Trace(TRACE_LEVEL_ERROR, "%!STATUS!", st);
                WdfRequestComplete(Request, st);
        }
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_default_queue(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);

        cfg.EvtIoDeviceControl = IoDeviceControl;
        cfg.PowerManaged = WdfFalse;

        auto ctx = get_vhci_context(vhci);

        if (auto err = WdfIoQueueCreate(vhci, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &ctx->default_queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}
