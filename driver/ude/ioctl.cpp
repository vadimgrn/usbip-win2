/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ioctl.h"
#include "trace.h"
#include "ioctl.tmh"

#include "usbdevice.h"
#include <usbip\vhci.h>

namespace
{
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto do_plugin_hardware(_In_ WDFDEVICE vhci, _Inout_ usbip::vhci::ioctl_plugin &r)
{
        PAGED_CODE();

        auto &error = r.port;
        error = ERR_PORTFULL;

        UDECXUSBDEVICE udev{};
        if (auto err = usbip::create_usbdevice(udev, vhci, UdecxUsbHighSpeed)) { // FIXME
                return err;
        }

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options;
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        if (auto err = UdecxUsbDevicePlugIn(udev, &options)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugIn %!STATUS!", err);
                WdfObjectDelete(udev);
                return err;
        }

        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::plugin_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugin *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        TraceMsg("%s:%s, busid %s, serial %s", r->host, r->service, r->busid, r->serial);
        
        auto queue = WdfRequestGetIoQueue(Request);
        auto vhci = WdfIoQueueGetDevice(queue);

        return do_plugin_hardware(vhci, *r);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::unplug_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();
        vhci::ioctl_unplug *r{};

        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        TraceMsg("Port #%d", r->port);
        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::get_imported_devices(_In_ WDFREQUEST Request)
{
        PAGED_CODE();
        vhci::ioctl_imported_dev *dev{};
        size_t buf_sz = 0;

        if (auto err = WdfRequestRetrieveOutputBuffer(Request, sizeof(*dev), &PVOID(dev), &buf_sz)) {
                return err;
        }

        auto cnt = buf_sz/sizeof(*dev);
        TraceMsg("Count %Iu", cnt);

        return STATUS_NOT_IMPLEMENTED;
}
