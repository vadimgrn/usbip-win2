/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "vhci_queue.h"
#include "trace.h"
#include "vhci_queue.tmh"

#include "driver.h"
#include "vhci.h"
#include "usbdevice.h"

#include <usbip\vhci.h>
#include <libdrv\dbgcommon.h>

#include <ws2def.h>
#include <ntstrsafe.h>

#include <usbuser.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace
{

using namespace usbip;

static_assert(sizeof(vhci::ioctl_plugin::service) == NI_MAXSERV);
static_assert(sizeof(vhci::ioctl_plugin::host) == NI_MAXHOST);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_vhci(_In_ WDFREQUEST Request)
{
        PAGED_CODE();
        auto queue = WdfRequestGetIoQueue(Request);
        return WdfIoQueueGetDevice(queue);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void to_ansi_str(_Out_ char *dest, _In_ USHORT len, _In_ const UNICODE_STRING &src)
{
        PAGED_CODE();
        ANSI_STRING s{ 0, len, dest };
        if (auto err = RtlUnicodeStringToAnsiString(&s, &src, false)) {
                Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringToAnsiString('%!USTR!') %!STATUS!", &src, err);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void fill(_Out_ vhci::ioctl_imported_dev &dst, _In_ const usbdevice_context &src)
{
        PAGED_CODE();

        dst.port = src.port;
        //RtlStringCbCopyA(dst.busid, sizeof(dst.busid), src.busid);

        //to_ansi_str(dst.service, sizeof(dst.service), src.service_name);
        //to_ansi_str(dst.host, sizeof(dst.host), src.node_name);
        //to_ansi_str(dst.serial, sizeof(dst.serial), src.serial);

        dst.status = SDEV_ST_USED;
        /*
        if (auto d = &vpdo->descriptor) {
        dst.vendor = d->idVendor;
        dst.product = d->idProduct;
        }

        dst.devid = vpdo->devid;
        dst.speed = vpdo->speed;
        */
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto plugin(_Out_ int &port, _In_ UDECXUSBDEVICE udev)
{
        PAGED_CODE();

        port = remember_usbdevice(udev);
        if (!port) {
                Trace(TRACE_LEVEL_ERROR, "All roothub ports are occupied");
                return ERR_PORTFULL;
        }

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options; 
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        if (auto err = UdecxUsbDevicePlugIn(udev, &options)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugIn %!STATUS!", err);
                return ERR_GENERAL;
        }

        return ERR_NONE;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void plugin_hardware(_In_ WDFDEVICE vhci, _Inout_ vhci::ioctl_plugin &r)
{
        PAGED_CODE();
        Trace(TRACE_LEVEL_INFORMATION, "%s:%s, busid %s, serial %s", r.host, r.service, r.busid, r.serial);

        auto &error = r.port;
        auto speed = UdecxUsbSuperSpeed; // FIXME

        UDECXUSBDEVICE udev{};

        if (NT_ERROR(create_usbdevice(udev, vhci, speed))) {
                error = make_error(ERR_GENERAL);
        } else if (auto err = plugin(r.port, udev)) {
                error = make_error(err);
                WdfObjectDelete(udev); // UdecxUsbDevicePlugIn failed or was not called
        } else {
                Trace(TRACE_LEVEL_INFORMATION, "udev %04x -> port %d", ptr04x(udev), r.port);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto plugin_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugin *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        if (auto vhci = get_vhci(Request)) {
                plugin_hardware(vhci, *r);
        }

        WdfRequestSetInformation(Request, sizeof(r->port));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto plugout_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugout *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        auto vhci = get_vhci(Request);
        auto err = STATUS_SUCCESS;

        if (r->port <= 0) {
                destroy_all_usbdevices(vhci);
        } else if (auto udev = get_usbdevice(vhci, r->port)) {
                destroy_usbdevice(udev.get<UDECXUSBDEVICE>());
        } else {
                Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", r->port);
                err = STATUS_NO_SUCH_DEVICE;
        }

        return err;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_imported_devices(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        size_t buf_sz = 0;
        vhci::ioctl_imported_dev *dev{};
        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*dev), &PVOID(dev), &buf_sz)) {
                return err;
        }

        auto cnt = buf_sz/sizeof(*dev);
        int result_cnt = 0;

        auto vhci = get_vhci(Request);

        for (int port = 1; port <= ARRAYSIZE(vhci_context::devices) && cnt; ++port) {
                if (auto udev = get_usbdevice(vhci, port)) {
                        auto &ctx = *get_usbdevice_context(udev.get());
                        fill(dev[result_cnt++], ctx);
                        --cnt;
                }
        }

        WdfRequestSetInformation(Request, result_cnt*sizeof(*dev));
        return STATUS_SUCCESS;
}

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
        TraceDbg("%s(%#08lX), OutputBufferLength %Iu, InputBufferLength %Iu", 
                  device_control_name(IoControlCode), IoControlCode, OutputBufferLength, InputBufferLength);

        USBUSER_REQUEST_HEADER *hdr{};
        auto st = STATUS_INVALID_DEVICE_REQUEST;
        auto complete = true;

        switch (IoControlCode) {
        case vhci::IOCTL_PLUGIN_HARDWARE:
                st = plugin_hardware(Request);
                break;
        case vhci::IOCTL_PLUGOUT_HARDWARE:
                st = plugout_hardware(Request);
                break;
        case vhci::IOCTL_GET_IMPORTED_DEVICES:
                st = get_imported_devices(Request);
                break;
        case IOCTL_USB_USER_REQUEST:
                if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*hdr), &PVOID(hdr), nullptr))) {
                        TraceDbg("USB_USER_REQUEST -> %s(%#08lX)", usbuser_request_name(hdr->UsbUserRequest), hdr->UsbUserRequest);
                }
                [[fallthrough]];
        default:
                complete = !UdecxWdfDeviceTryHandleUserIoctl(WdfIoQueueGetDevice(Queue), Request);
        }

        if (complete) {
                TraceDbg("%!STATUS!, Information %Iu", st, WdfRequestGetInformation(Request));
                WdfRequestComplete(Request, st);
        }
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::create_default_queue(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);

        cfg.EvtIoDeviceControl = IoDeviceControl;
        cfg.PowerManaged = WdfFalse;

        auto ctx = get_vhci_context(vhci);

        if (auto err = WdfIoQueueCreate(vhci, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &ctx->queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}
