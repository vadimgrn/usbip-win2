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

#include <ws2def.h>
#include <ntstrsafe.h>

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
PAGEABLE auto plugin(_Out_ int &port, _In_ UDECXUSBDEVICE udev, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        port = claim_roothub_port(udev, speed);
        if (!port) {
                Trace(TRACE_LEVEL_ERROR, "All roothub ports are occupied");
                return ERR_PORTFULL;
        }

        UDECX_USB_DEVICE_PLUG_IN_OPTIONS options;
        UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);

        auto &num = vhci::get_hci_version(port) == vhci::HCI_USB3 ? options.Usb30PortNumber : options.Usb20PortNumber;
        num = port;
        
        if (auto err = UdecxUsbDevicePlugIn(udev, &options)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugIn %!STATUS!, port %lu", err, num);
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
        } else if (auto err = plugin(r.port, udev, speed)) {
                error = make_error(err);
                WdfObjectDelete(udev); // UdecxUsbDevicePlugIn failed or was not called
        } else {
                Trace(TRACE_LEVEL_INFORMATION, "udev %04x -> port %d", ptr04x(udev), r.port);
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

        auto vhci = get_vhci(Request);
        ::plugin_hardware(vhci, *r);

        WdfRequestSetInformation(Request, sizeof(r->port));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::plugout_hardware(_In_ WDFREQUEST Request)
{
        PAGED_CODE();

        vhci::ioctl_plugout *r{};
        if (auto err = WdfRequestRetrieveInputBuffer(Request, sizeof(*r), &PVOID(r), nullptr)) {
                return err;
        }

        auto vhci = get_vhci(Request);

        if (r->port > 0) {
                if (auto udev = get_usbdevice(vhci, r->port)) {
                        destroy_usbdevice(udev.get<UDECXUSBDEVICE>());
                        return STATUS_SUCCESS;
                }
                Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", r->port);
                return STATUS_NO_SUCH_DEVICE;
        }

        for (int port = 1; port <= ARRAYSIZE(vhci_context::devices); ++port) {
                if (auto udev = get_usbdevice(vhci, port)) {
                        destroy_usbdevice(udev.get<UDECXUSBDEVICE>());
                }
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::get_imported_devices(_In_ WDFREQUEST Request)
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
                        auto ctx = get_usbdevice_context(udev.get());
                        fill(*dev, *ctx);
                        ++result_cnt;
                        ++dev;
                        --cnt;
                }
        }

        WdfRequestSetInformation(Request, result_cnt*sizeof(*dev));
        return STATUS_SUCCESS;
}
