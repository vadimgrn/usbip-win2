/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "ioctl.h"
#include "trace.h"
#include "ioctl.tmh"

#include "driver.h"
#include "vhci.h"
#include "usbdevice.h"
#include <usbip\vhci.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto plugin(_Out_ int &port, _In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE udev, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        port = assign_hub_port(vhci, udev, speed);

        if (!vhci::is_valid_vport(port)) {
                Trace(TRACE_LEVEL_ERROR, "All hub ports are occupied");
                return ERR_PORTFULL;
        }

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options;
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        if (auto ctx = get_usbdevice_context(udev)) {
                auto &num = vhci::get_hci_version(port) == vhci::HCI_USB3 ? 
                            options.Usb30PortNumber : options.Usb20PortNumber;

                num = vhci::get_rhport(port);
        }
        
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

        auto &error = r.port;
        auto speed = UdecxUsbHighSpeed; // FIXME

        UDECXUSBDEVICE udev{};

        if (NT_ERROR(create_usbdevice(udev, vhci, speed))) {
                error = ERR_GENERAL;
        } else if (auto err = plugin(r.port, vhci, udev, speed)) {
                error = err;
                WdfObjectDelete(udev);
        } else {
                TraceMsg("udev %04x -> port #%d", ptr4log(udev), r.port);
        }
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
        
        if (auto vhci = WdfIoQueueGetDevice(queue)) {
                ::plugin_hardware(vhci, *r);
        }

        return STATUS_SUCCESS;
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
